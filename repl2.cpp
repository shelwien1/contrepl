//#include "mim-include/mimalloc-new-delete.h"

#define _FILE_OFFSET_BITS 64
#define PCRE2_CODE_UNIT_WIDTH 8

#include <pcre2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <string_view>
#include <unordered_map>
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

string regex_quote(string_view s) {
  string result;
  result.reserve(s.length() * 2);
  for (size_t i = 0; i < s.length(); i++) {
    char c;
    c = s[i];
    if (strchr(".^$*+?()[{\\|", c)) {
      result += '\\';
    }
    result += c;
  }
  return result;
}

string build_alternation(const vector<string_view> &keys) {
  vector<pair<size_t, string_view>> sorted;
  string result;
  size_t i;

  sorted.reserve(keys.size());
  for (i = 0; i < keys.size(); i++) {
    sorted.push_back(make_pair(keys[i].length(), keys[i]));
  }

  // Sort by length descending
  for (i = 0; i < sorted.size(); i++) {
    size_t j;
    for (j = i + 1; j < sorted.size(); j++) {
      if (sorted[j].first > sorted[i].first) {
        pair<size_t, string_view> temp;
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
  string lb, la, original, intermediate;
  vector<ReplacementPair> pairs;
  unordered_map<string_view, string_view> forward, backward;
  vector<string_view> forward_keys, backward_keys;
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

  // Reserve space for vectors
  forward_keys.reserve(pairs.size());
  backward_keys.reserve(pairs.size());

  // Build forward and backward maps (using string_view references to pairs)
  for (i = 0; i < pairs.size(); i++) {
    string_view from_view(pairs[i].from);
    string_view to_view(pairs[i].to);
    forward[from_view] = to_view;
    forward_keys.push_back(from_view);

    if (backward.find(to_view) == backward.end()) {
      backward[to_view] = from_view;
      backward_keys.push_back(to_view);
    }
  }

  original = read_file(in_file);

  // Reserve space for intermediate output
  intermediate.reserve(original.length());

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
    }

    // Add replacement
    string_view match_str(original.data() + start, end - start);
    string_view repl = forward[match_str];
    intermediate += repl;

    last_end = end;
    offset = end;
    if (offset == start)
      offset++;
  }

  // Add remaining portion
  if (last_end < original.length()) {
    intermediate += original.substr(last_end);
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
  // Since original.substr(0,sim_pos)==simulated.substr(0,sim_pos) is always true,
  // we can use sim_pos directly as the position in original
  string simulated;
  simulated = intermediate;

  // Open flags file for writing
  FILE *flg;
  flg = fopen(flg_file, "wb");
  if (!flg) {
    fprintf(stderr, "Cannot open %s for writing\n", flg_file);
    exit(1);
  }
  qword flag_count = 0;

  offset = 0;
  vector<bool> seen_sim_pos(simulated.length(), false);

  while (offset < simulated.length()) {
    rc = pcre2_match(bwd_re, (PCRE2_SPTR)simulated.c_str(), simulated.length(), offset, 0, match_data, NULL);
    if( rc<0 ) break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE sim_pos, sim_end;
    sim_pos = ovector[0];
    sim_end = ovector[1];

    if (!seen_sim_pos[sim_pos]) {
      seen_sim_pos[sim_pos] = true;

      string_view match_str(simulated.data() + sim_pos, sim_end - sim_pos);
      string_view repl = backward[match_str];

      // Use sim_pos directly as position in original (they're always equal up to sim_pos)
      bool should;
      should = false;
      if (sim_pos + repl.length() <= original.length()) {
        string_view orig_view(original.data() + sim_pos, repl.length());
        should = (orig_view == repl);
      }

      char flag_char = should ? '1' : '0';
      fwrite(&flag_char, 1, 1, flg);
      flag_count++;

      if (should) {
        qword match_len, repl_len;
        match_len = sim_end - sim_pos;
        repl_len = repl.length();

        // Replace in simulated string
        simulated.replace(sim_pos, match_len, repl);

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
  fclose(flg);

  // Write output file
  FILE *out;
  out = fopen(out_file, "wb");
  if (!out) {
    fprintf(stderr, "Cannot open %s for writing\n", out_file);
    exit(1);
  }
  fwrite(intermediate.c_str(), 1, intermediate.length(), out);
  fclose(out);

  fprintf(stderr, "Original: %lu bytes, Output: %lu bytes, Flags: %lu\n", (qword)original.length(), (qword)intermediate.length(), (qword)flag_count);
}

void mode_decompress(const char *cfg_file, const char *in_file, const char *out_file, const char *flg_file) {
  string lb, la, data;
  vector<ReplacementPair> pairs;
  unordered_map<string_view, string_view> backward;
  vector<string_view> backward_keys;
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

  // Reserve space for vectors
  backward_keys.reserve(pairs.size());

  // Build backward map (using string_view references to pairs)
  for (i = 0; i < pairs.size(); i++) {
    string_view from_view(pairs[i].from);
    string_view to_view(pairs[i].to);
    if (backward.find(to_view) == backward.end()) {
      backward[to_view] = from_view;
      backward_keys.push_back(to_view);
    }
  }

  data = read_file(in_file);
  in_len = data.length();

  // Open flags file for reading
  FILE *flg;
  flg = fopen(flg_file, "rb");
  if (!flg) {
    fprintf(stderr, "Cannot open %s for reading\n", flg_file);
    exit(1);
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

      int flag_char = fgetc(flg);
      if (flag_char != EOF) {
        should_replace = (flag_char == '1');
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
      string_view match_str(data.data() + pos, end - pos);
      string_view repl = backward[match_str];
      fwrite(repl.data(), 1, repl.length(), out);
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
  fclose(flg);
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
