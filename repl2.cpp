//#include "mim-include/mimalloc-new-delete.h"

#define _FILE_OFFSET_BITS 64
#define PCRE2_CODE_UNIT_WIDTH 8

#include <map>
#include <pcre2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

//#define pcre2_jit_compile(x,y) 0

#ifdef _WIN32
#define off64_t __int64
#define ftello64 _ftelli64
#define fseeko64 _fseeki64
#elif defined(__APPLE__) || defined(__CYGWIN__)
#define off64_t off_t
#define ftello64 ftello
#define fseeko64 fseeko
#endif

using namespace std;

typedef uint64_t qword;
typedef uint32_t uint;
typedef uint16_t word;
typedef uint8_t byte;

struct ReplacementPair {
  string from;
  string to;
};

void decode_escapes(string &s) {
  string result;
  size_t i, len;
  len = s.length();
  i = 0;

  while (i < len) {
    if (s[i] == '\\' && i + 1 < len) {
      if (s[i + 1] == 'x' && i + 3 < len) {
        char hex[3];
        hex[0] = s[i + 2];
        hex[1] = s[i + 3];
        hex[2] = 0;
        result += (char)strtol(hex, NULL, 16);
        i += 4;
      } else if (s[i + 1] == 't') {
        result += '\t';
        i += 2;
      } else if (s[i + 1] == 'n') {
        result += '\n';
        i += 2;
      } else if (s[i + 1] == 'r') {
        result += '\r';
        i += 2;
      } else if (s[i + 1] == '\\') {
        result += '\\';
        i += 2;
      } else {
        result += s[i];
        i++;
      }
    } else {
      result += s[i];
      i++;
    }
  }
  s = result;
}

string read_file(const char *path) {
  FILE *f;
  qword size;
  char *buffer;
  string result;

  f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Cannot open %s\n", path);
    exit(1);
  }

  fseeko64(f, 0, SEEK_END);
  size = ftello64(f);
  fseeko64(f, 0, SEEK_SET);

  buffer = (char *)malloc(size + 1);
  if (!buffer) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(1);
  }

  fread(buffer, 1, size, f);
  buffer[size] = 0;
  fclose(f);

  result.assign(buffer, size);
  free(buffer);
  return result;
}

void parse_config(const char *cfg_file, string &lb, string &la, vector<ReplacementPair> &pairs) {
  string cfg_data;
  size_t pos, line_start, line_end;
  int line_num;

  cfg_data = read_file(cfg_file);

  // Normalize line endings
  pos = 0;
  while ((pos = cfg_data.find("\r\n", pos)) != string::npos) {
    cfg_data.replace(pos, 2, "\n");
  }

  // Parse lines
  line_num = 0;
  line_start = 0;

  while (line_start < cfg_data.length()) {
    line_end = cfg_data.find('\n', line_start);
    if (line_end == string::npos) {
      line_end = cfg_data.length();
    }

    string line = cfg_data.substr(line_start, line_end - line_start);

    if (line_num == 0) {
      lb = line;
    } else if (line_num == 1) {
      la = line;
    } else if (!line.empty() && line.find('\t') != string::npos) {
      size_t tab_pos;
      tab_pos = line.find('\t');
      ReplacementPair pair;
      pair.from = line.substr(0, tab_pos);
      pair.to = line.substr(tab_pos + 1);
      decode_escapes(pair.from);
      decode_escapes(pair.to);
      pairs.push_back(pair);
    }

    line_start = line_end + 1;
    line_num++;
  }
}

string regex_quote(const string &s) {
  string result;
  size_t i;
  for (i = 0; i < s.length(); i++) {
    char c;
    c = s[i];
    if (strchr(".^$*+?()[{\\|", c)) {
      result += '\\';
    }
    result += c;
  }
  return result;
}

string build_alternation(const vector<string> &keys) {
  vector<pair<size_t, string>> sorted;
  string result;
  size_t i;

  for (i = 0; i < keys.size(); i++) {
    sorted.push_back(make_pair(keys[i].length(), keys[i]));
  }

  // Sort by length descending
  for (i = 0; i < sorted.size(); i++) {
    size_t j;
    for (j = i + 1; j < sorted.size(); j++) {
      if (sorted[j].first > sorted[i].first) {
        pair<size_t, string> temp;
        temp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = temp;
      }
    }
  }

  for (i = 0; i < sorted.size(); i++) {
    if (i > 0)
      result += '|';
    result += regex_quote(sorted[i].second);
  }

  return result;
}

void mode_compress(const char *cfg_file, const char *in_file, const char *out_file, const char *flg_file) {
  string lb, la, original, intermediate, flags;
  vector<ReplacementPair> pairs;
  map<string, string> forward, backward;
  vector<string> forward_keys, backward_keys;
  vector<qword> orig_pos;
  pcre2_code *fwd_re;
  pcre2_code *bwd_re;
  pcre2_match_data *match_data;
  int rc;
  PCRE2_SIZE *ovector;
  PCRE2_SIZE offset;
  string fwd_pattern, bwd_pattern;
  size_t i;

  parse_config(cfg_file, lb, la, pairs);

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config\n");
    exit(1);
  }

  // Build forward and backward maps
  for (i = 0; i < pairs.size(); i++) {
    forward[pairs[i].from] = pairs[i].to;
    forward_keys.push_back(pairs[i].from);

    if (backward.find(pairs[i].to) == backward.end()) {
      backward[pairs[i].to] = pairs[i].from;
      backward_keys.push_back(pairs[i].to);
    }
  }

  original = read_file(in_file);

  // Build forward regex pattern
  fwd_pattern = "(?<=" + lb + ")(" + build_alternation(forward_keys) + ")(?=" + la + ")";

  // Compile forward regex
  int errcode=-1;

  pcre2_config(PCRE2_CONFIG_JIT, &errcode);
printf( "!JIT=%i!\n", errcode );

  PCRE2_SIZE erroffset;
  fwd_re = pcre2_compile((PCRE2_SPTR)fwd_pattern.c_str(), PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  pcre2_jit_compile(fwd_re, PCRE2_JIT_COMPLETE);

  if (!fwd_re) {
    fprintf(stderr, "PCRE2 compilation failed\n");
    exit(1);
  }

  match_data = pcre2_match_data_create_from_pattern(fwd_re, NULL);

  // Forward replacement with position tracking
  offset = 0;
  qword last_end;
  last_end = 0;

  while (offset < original.length()) {
    rc = pcre2_match(fwd_re, (PCRE2_SPTR)original.c_str(), original.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start, end;
    start = ovector[0];
    end = ovector[1];

    // Add unmatched portion
    if (start > last_end) {
      intermediate += original.substr(last_end, start - last_end);
      for (qword p = last_end; p < start; p++) {
        orig_pos.push_back(p);
      }
    }

    // Add replacement
    string match_str;
    match_str = original.substr(start, end - start);
    string repl;
    repl = forward[match_str];
    intermediate += repl;
    for (size_t r = 0; r < repl.length(); r++) {
      orig_pos.push_back(start);
    }

    last_end = end;
    offset = end;
    if (offset == start)
      offset++;
  }

  // Add remaining portion
  if (last_end < original.length()) {
    intermediate += original.substr(last_end);
    for (qword p = last_end; p < original.length(); p++) {
      orig_pos.push_back(p);
    }
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(fwd_re);

  // Build backward regex pattern
  bwd_pattern = "(?<=" + lb + ")(" + build_alternation(backward_keys) + ")(?=" + la + ")";

  bwd_re = pcre2_compile((PCRE2_SPTR)bwd_pattern.c_str(), PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  pcre2_jit_compile(bwd_re, PCRE2_JIT_COMPLETE);

  if (!bwd_re) {
    fprintf(stderr, "PCRE2 compilation failed\n");
    exit(1);
  }

  match_data = pcre2_match_data_create_from_pattern(bwd_re, NULL);

  // Simulate backward replacement to generate flags
  // We need to actually modify a simulated string to match Perl behavior
  string simulated;
  vector<qword> sim_to_int_pos;
  simulated = intermediate;

  // Initially, simulated position maps 1:1 to intermediate position
  for (i = 0; i < intermediate.length(); i++) {
    sim_to_int_pos.push_back(i);
  }

  offset = 0;
  map<qword, bool> seen_sim_pos;

  while (offset < simulated.length()) {
    rc = pcre2_match(bwd_re, (PCRE2_SPTR)simulated.c_str(), simulated.length(), offset, 0, match_data, NULL);
    if( rc<0 ) break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE sim_pos, sim_end;
    sim_pos = ovector[0];
    sim_end = ovector[1];

//printf( "%6i: %i\n", int(sim_pos), int(original.substr(0,sim_pos)==simulated.substr(0,sim_pos)) );

    // Map simulated position back to intermediate position
    qword int_pos;
    int_pos = sim_to_int_pos[sim_pos];

    if (seen_sim_pos.find(sim_pos) == seen_sim_pos.end()) {
      seen_sim_pos[sim_pos] = true;

      string match_str;
      match_str = simulated.substr(sim_pos, sim_end - sim_pos);
      string repl;
      repl = backward[match_str];

      qword orig_start;
      orig_start = orig_pos[int_pos];

      bool should;
      should = false;
      if (orig_start + repl.length() <= original.length()) {
        string orig_substr;
        orig_substr = original.substr(orig_start, repl.length());
        should = (orig_substr == repl);
      }

      flags += should ? '1' : '0';

      if (should) {
        qword match_len, repl_len;
        match_len = sim_end - sim_pos;
        repl_len = repl.length();

        // Replace in simulated string (more efficient than concatenation)
        simulated.replace(sim_pos, match_len, repl);

        // Update position mapping more efficiently
        if (repl_len != match_len) {
          if (repl_len > match_len) {
            // Insert new positions
            sim_to_int_pos.insert(sim_to_int_pos.begin() + sim_end, repl_len - match_len, int_pos);
          } else {
            // Remove positions
            sim_to_int_pos.erase(sim_to_int_pos.begin() + sim_pos + repl_len, sim_to_int_pos.begin() + sim_end);
          }
        }

        // Update existing positions in the replacement range
        for (qword j = sim_pos; j < sim_pos + repl_len; j++) {
          sim_to_int_pos[j] = int_pos;
        }

        offset = sim_pos + repl_len;
      } else {
        offset = sim_pos + 1;
      }
    } else {
      offset = sim_pos + 1;
    }
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);

  // Write output file
  FILE *out;
  out = fopen(out_file, "wb");
  if (!out) {
    fprintf(stderr, "Cannot open %s for writing\n", out_file);
    exit(1);
  }
  fwrite(intermediate.c_str(), 1, intermediate.length(), out);
  fclose(out);

  // Write flags file
  FILE *flg;
  flg = fopen(flg_file, "wb");
  if (!flg) {
    fprintf(stderr, "Cannot open %s for writing\n", flg_file);
    exit(1);
  }
  fwrite(flags.c_str(), 1, flags.length(), flg);
  fclose(flg);

  fprintf(stderr, "Original: %lu bytes, Output: %lu bytes, Flags: %lu\n", (qword)original.length(), (qword)intermediate.length(), (qword)flags.length());
}

void mode_decompress(const char *cfg_file, const char *in_file, const char *out_file, const char *flg_file) {
  string lb, la, data;
  vector<bool> flags;
  vector<ReplacementPair> pairs;
  map<string, string> backward;
  vector<string> backward_keys;
  pcre2_code *bwd_re;
  pcre2_match_data *match_data;
  int rc;
  PCRE2_SIZE *ovector;
  PCRE2_SIZE offset;
  string bwd_pattern;
  size_t i;
  qword in_len;

  parse_config(cfg_file, lb, la, pairs);

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config\n");
    exit(1);
  }

  // Build backward map
  for (i = 0; i < pairs.size(); i++) {
    if (backward.find(pairs[i].to) == backward.end()) {
      backward[pairs[i].to] = pairs[i].from;
      backward_keys.push_back(pairs[i].to);
    }
  }

  data = read_file(in_file);
  in_len = data.length();

  // Read flags into vector<bool>
  string flags_str;
  flags_str = read_file(flg_file);
  for (i = 0; i < flags_str.length(); i++) {
    flags.push_back(flags_str[i] == '1');
  }

  // Build backward regex pattern
  bwd_pattern = "(?<=" + lb + ")(" + build_alternation(backward_keys) + ")(?=" + la + ")";

  int errcode;
  PCRE2_SIZE erroffset;
  bwd_re = pcre2_compile((PCRE2_SPTR)bwd_pattern.c_str(), PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  pcre2_jit_compile(bwd_re, PCRE2_JIT_COMPLETE);

  if (!bwd_re) {
    fprintf(stderr, "PCRE2 compilation failed\n");
    exit(1);
  }

  match_data = pcre2_match_data_create_from_pattern(bwd_re, NULL);

  // Open output file for writing
  FILE *out;
  out = fopen(out_file, "wb");
  if (!out) {
    fprintf(stderr, "Cannot open %s for writing\n", out_file);
    exit(1);
  }

  // Apply replacements using flags, write directly to file
  offset = 0;
  qword last_end;
  qword flag_idx;
  qword bytes_written;
  last_end = 0;
  flag_idx = 0;
  bytes_written = 0;
  vector<bool> seen_pos(data.length(), false);

  while (offset < data.length()) {
    rc = pcre2_match(bwd_re, (PCRE2_SPTR)data.c_str(), data.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE pos, end;
    pos = ovector[0];
    end = ovector[1];

    bool should_replace;
    should_replace = false;

    if (!seen_pos[pos]) {
      seen_pos[pos] = true;

      if (flag_idx < flags.size()) {
        should_replace = flags[flag_idx];
        flag_idx++;
      }
    }

    if (should_replace) {
      // Write unmatched portion before this match
      if (pos > last_end) {
        qword len;
        len = pos - last_end;
        fwrite(data.c_str() + last_end, 1, len, out);
        bytes_written += len;
      }

      // Write replacement
      string match_str;
      match_str = data.substr(pos, end - pos);
      string repl;
      repl = backward[match_str];
      fwrite(repl.c_str(), 1, repl.length(), out);
      bytes_written += repl.length();

      last_end = end;
      offset = end;
    } else {
      // Don't replace, continue from next position
      offset = pos + 1;
    }
  }

  // Write remaining portion
  if (last_end < data.length()) {
    qword len;
    len = data.length() - last_end;
    fwrite(data.c_str() + last_end, 1, len, out);
    bytes_written += len;
  }

  fclose(out);
  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);

  fprintf(stderr,
          "Input: %lu bytes, Restored: %lu bytes, "
          "Flags used: %lu\n",
          (qword)in_len, (qword)bytes_written, (qword)flag_idx);
}

int main(int argc, char **argv) {
  if (argc != 6) {
    fprintf(stderr,
            "Usage: %s <mode> <config> <input> <output> <flags>\n"
            "Modes:\n"
            "  c - compress (forward replacement with flag generation)\n"
            "  d - decompress (reverse replacement using flags)\n"
            "Examples:\n"
            "  %s c book1.cfg book1 book1.out book1.flg\n"
            "  %s d book1.cfg book1.out book1.rst book1.flg\n",
            argv[0], argv[0], argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "c") == 0) {
    mode_compress(argv[2], argv[3], argv[4], argv[5]);
  } else if (strcmp(argv[1], "d") == 0) {
    mode_decompress(argv[2], argv[3], argv[4], argv[5]);
  } else {
    fprintf(stderr, "Invalid mode '%s'. Use 'c' or 'd'.\n", argv[1]);
    return 1;
  }

  return 0;
}
