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
#include <algorithm>

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

// Context size constants for API
static const int CTX_BEFORE = 32;  // symbols before match
static const int CTX_AFTER = 32;   // symbols after match

// API function for flags I/O
// bit=-1: constructor, ctx=filename, ofs=mode (0=encode/write, 1=decode/read)
// bit=-2: destructor
// bit=-3: read flag (decode mode), returns 0/1 or -1 on EOF
// bit>=0: write flag (encode mode)
static FILE* api_flg = nullptr;
static int api_mode = 0;  // 0=encode, 1=decode

int API(char bit, const char* ctx = 0, int ofs = 0, int len = 0, int mlen = 0) {
  if (bit == -1) {
    // Constructor: open flags file
    api_mode = ofs;
    if (api_mode == 0) {
      // Encode mode - open for writing
      api_flg = fopen(ctx, "wb");
    } else {
      // Decode mode - open for reading
      api_flg = fopen(ctx, "rb");
    }
    if (!api_flg) {
      fprintf(stderr, "Cannot open flags file %s\n", ctx);
      return 1;  // error
    }
    return 0;  // success
  } else if (bit == -2) {
    // Destructor: close file
    if (api_flg) {
      fclose(api_flg);
      api_flg = nullptr;
    }
    return 0;
  } else if (bit == -3) {
    // Read flag (decode mode)
    if (!api_flg) return -1;
    int c = fgetc(api_flg);
    if (c == EOF) return -1;
    return (c == '1') ? 1 : 0;
  } else {
    // Write flag (encode mode)
    if (!api_flg) return -1;
    fputc(bit ? '1' : '0', api_flg);
    return 0;
  }
}

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

  // Sort by length descending using std::sort (O(n log n) instead of O(n^2))
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    return a.first > b.first;
  });

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

  pcre2_config(PCRE2_CONFIG_JIT, &rc);
printf( "!JIT=%i!\n", rc );

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
    rc = pcre2_jit_match(fwd_re, (PCRE2_SPTR)original.c_str(), original.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start, end;
    start = ovector[0];
    end = ovector[1];

    // Add unmatched portion
    if (start > last_end) {
      intermediate.append(original.data() + last_end, start - last_end);
    }

    // Add replacement
    string_view match_str(original.data() + start, end - start);
    string_view repl = forward[match_str];
    intermediate.append(repl.data(), repl.length());

    last_end = end;
    offset = end;
    if (offset == start)
      offset++;
  }

  // Add remaining portion
  if (last_end < original.length()) {
    intermediate.append(original.data() + last_end, original.length() - last_end);
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

  // Two-pass approach to avoid O(n) string::replace operations
  // Pass 1: Collect all match positions in intermediate
  // Pass 2: Process matches sequentially, tracking cumulative offset

  // Open flags file for writing via API
  if (API(-1, flg_file, 0) != 0) {
    exit(1);
  }
  qword flags_count = 0;

  // Pass 1: Collect all matches in intermediate
  vector<pair<PCRE2_SIZE, PCRE2_SIZE>> matches;
  matches.reserve(intermediate.length() / 4);
  offset = 0;

  while (offset < intermediate.length()) {
    rc = pcre2_jit_match(bwd_re, (PCRE2_SPTR)intermediate.c_str(), intermediate.length(), offset, 0, match_data, NULL);
    if (rc < 0) break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start = ovector[0];
    PCRE2_SIZE end = ovector[1];
    matches.push_back({start, end});
    offset = start + 1;
  }

  // Pass 2: Process matches and compute flags
  // Track cumulative offset: difference between position in simulated vs intermediate
  int64_t cumulative_delta = 0;
  PCRE2_SIZE next_valid_int_pos = 0;  // Skip matches before this position in intermediate

  for (size_t match_idx = 0; match_idx < matches.size(); match_idx++) {
    PCRE2_SIZE int_pos = matches[match_idx].first;
    PCRE2_SIZE int_end = matches[match_idx].second;

    // Skip matches that fall within a previously replaced region
    if (int_pos < next_valid_int_pos) continue;

    // Position in simulated (and original) = position in intermediate + cumulative delta
    PCRE2_SIZE sim_pos = int_pos + cumulative_delta;
    PCRE2_SIZE match_len = int_end - int_pos;

    string_view match_str(intermediate.data() + int_pos, match_len);
    string_view repl = backward[match_str];

    // Check if original at sim_pos matches the replacement
    bool should = false;
    if (sim_pos + repl.length() <= original.length()) {
      string_view orig_view(original.data() + sim_pos, repl.length());
      should = (orig_view == repl);
    }

    // Calculate context for API call
    int ctx_before = (int_pos >= CTX_BEFORE) ? CTX_BEFORE : (int)int_pos;
    int ctx_after = ((int_pos + match_len + CTX_AFTER) <= intermediate.length())
                    ? CTX_AFTER : (int)(intermediate.length() - int_pos - match_len);
    const char* context = intermediate.c_str() + int_pos - ctx_before;
    int ctx_ofs = ctx_before;
    int ctx_len = ctx_before + (int)match_len + ctx_after;

    API(should ? 1 : 0, context, ctx_ofs, ctx_len, (int)match_len);
    flags_count++;

    if (should) {
      // Update cumulative delta: we're replacing match_len with repl.length()
      cumulative_delta += (int64_t)repl.length() - (int64_t)match_len;
      // Skip all matches that start before int_pos + match_len
      next_valid_int_pos = int_end;
    }
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);
  API(-2);  // Close flags file

  // Write output file
  FILE *out;
  out = fopen(out_file, "wb");
  if (!out) {
    fprintf(stderr, "Cannot open %s for writing\n", out_file);
    exit(1);
  }
  fwrite(intermediate.c_str(), 1, intermediate.length(), out);
  fclose(out);

  fprintf(stderr, "Original: %lu bytes, Output: %lu bytes, Flags: %lu\n", (qword)original.length(), (qword)intermediate.length(), (qword)flags_count);
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

  // Open flags file for reading via API
  if (API(-1, flg_file, 1) != 0) {
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
  vector<char> seen_pos(data.length(), 0);

  while (offset < data.length()) {
    rc = pcre2_jit_match(bwd_re, (PCRE2_SPTR)data.c_str(), data.length(), offset, 0, match_data, NULL);

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

      // Calculate context for API call
      PCRE2_SIZE match_len = end - pos;
      int ctx_before = (pos >= CTX_BEFORE) ? CTX_BEFORE : (int)pos;
      int ctx_after = ((pos + match_len + CTX_AFTER) <= data.length())
                      ? CTX_AFTER : (int)(data.length() - pos - match_len);
      const char* context = data.c_str() + pos - ctx_before;
      int ctx_ofs = ctx_before;
      int ctx_len = ctx_before + (int)match_len + ctx_after;

      int c = API(-3, context, ctx_ofs, ctx_len, (int)match_len);
      if (c != -1) {
        should_replace = (c == 1);
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
  API(-2);  // Close flags file
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
