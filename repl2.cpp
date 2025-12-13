//#include "mim-include/mimalloc-new-delete.h"

#define _FILE_OFFSET_BITS 64
#define PCRE2_CODE_UNIT_WIDTH 8

#define byte byte1
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
#undef byte

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

typedef unsigned long long qword;
typedef unsigned int uint;
typedef unsigned short word;
typedef unsigned char byte;

// Context size constants for API
static const int CTX_BEFORE = 32;  // symbols before match
static const int CTX_AFTER = 32;   // symbols after match

// API function pointer type
// bit=-1: constructor, ctx=filename, ofs=mode (0=encode/write, 1=decode/read)
// bit=-2: destructor
// bit=-3: read flag (decode mode), returns 0/1 or -1 on EOF
// bit>=0: write flag (encode mode)
typedef int (*API_func)(char bit, const char* ctx, int ofs, int len, int mlen);

// Global API function pointer (loaded from DLL)
static API_func API = nullptr;

static bool load_dll(const char* dll_name);
static void unload_dll();

// Structure to hold a single flag record for caching
struct FlagRecord {
  int flag;       // 0 or 1
  string context; // context string
  int ctx_ofs;    // offset within context
  int ctx_len;    // context length
  int match_len;  // match length
};

struct ReplacementPair {
  string from;
  string to;
};

// Structure to hold a parsed config
struct ParsedConfig {
  string name;  // config file name (for logging)
  string lb;    // lookbehind pattern
  string la;    // lookahead pattern
  vector<ReplacementPair> pairs;
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

// Parse config from string data
void parse_config_data(const string& cfg_data_in, string &lb, string &la, vector<ReplacementPair> &pairs) {
  string cfg_data = cfg_data_in;
  size_t pos, line_start, line_end;
  int line_num;

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

// Parse config from file (wrapper that reads file then parses)
void parse_config(const char *cfg_file, string &lb, string &la, vector<ReplacementPair> &pairs) {
  string cfg_data = read_file(cfg_file);
  parse_config_data(cfg_data, lb, la, pairs);
}

// Parse a list file and load all configs into memory
vector<ParsedConfig> parse_list_file(const char* list_path) {
  vector<ParsedConfig> configs;
  string list_data = read_file(list_path);

  // Normalize line endings
  size_t pos = 0;
  while ((pos = list_data.find("\r\n", pos)) != string::npos) {
    list_data.replace(pos, 2, "\n");
  }

  // Parse lines to get config file paths
  vector<string> config_paths;
  size_t line_start = 0;
  while (line_start < list_data.length()) {
    size_t line_end = list_data.find('\n', line_start);
    if (line_end == string::npos) {
      line_end = list_data.length();
    }

    string line = list_data.substr(line_start, line_end - line_start);
    // Trim whitespace
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
      line.pop_back();
    }
    size_t start = 0;
    while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
      start++;
    }
    if (start > 0) {
      line = line.substr(start);
    }

    if (!line.empty()) {
      config_paths.push_back(line);
    }

    line_start = line_end + 1;
  }

  // Load and parse each config file
  for (const string& path : config_paths) {
    ParsedConfig cfg;
    cfg.name = path;
    string cfg_data = read_file(path.c_str());
    parse_config_data(cfg_data, cfg.lb, cfg.la, cfg.pairs);
    configs.push_back(std::move(cfg));
  }

  return configs;
}

// Parse multi-config file data - configs are separated by empty lines among pairs
// Returns a vector of configs
vector<ParsedConfig> parse_multi_config_data(const string& cfg_data_in, const string& base_name) {
  vector<ParsedConfig> configs;
  string cfg_data = cfg_data_in;
  size_t pos, line_start, line_end;
  int line_num;

  // Normalize line endings
  pos = 0;
  while ((pos = cfg_data.find("\r\n", pos)) != string::npos) {
    cfg_data.replace(pos, 2, "\n");
  }

  // Current config being built
  ParsedConfig current;
  current.name = base_name;
  int config_index = 0;
  bool in_pairs = false;  // Track if we've started reading pairs

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
      current.lb = line;
    } else if (line_num == 1) {
      current.la = line;
    } else {
      // We're in the pairs section
      if (line.empty()) {
        // Empty line signals end of current config (only if we have pairs)
        if (!current.pairs.empty()) {
          configs.push_back(std::move(current));
          config_index++;
          current = ParsedConfig();
          current.name = base_name + "[" + to_string(config_index) + "]";
          line_num = -1;  // Reset for next config (will be incremented to 0)
          in_pairs = false;
        }
      } else if (line.find('\t') != string::npos) {
        size_t tab_pos;
        tab_pos = line.find('\t');
        ReplacementPair pair;
        pair.from = line.substr(0, tab_pos);
        pair.to = line.substr(tab_pos + 1);
        decode_escapes(pair.from);
        decode_escapes(pair.to);
        current.pairs.push_back(pair);
        in_pairs = true;
      }
    }

    line_start = line_end + 1;
    line_num++;
  }

  // Add the last config if it has pairs
  if (!current.pairs.empty()) {
    configs.push_back(std::move(current));
  }

  return configs;
}

// Create ParsedConfigs from a file path (handles multi-config files)
vector<ParsedConfig> load_single_config(const char* cfg_file) {
  string cfg_data = read_file(cfg_file);
  return parse_multi_config_data(cfg_data, cfg_file);
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

// Compress with a single config - works on in-memory data
// Returns flags in flags_out, modifies data in-place
void compress_single(const ParsedConfig& cfg, const string& original, string& intermediate,
                     vector<FlagRecord>& flags_out) {
  const string& lb = cfg.lb;
  const string& la = cfg.la;
  const vector<ReplacementPair>& pairs = cfg.pairs;
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

  pcre2_config(PCRE2_CONFIG_JIT, &rc);
  //printf( "!JIT=%i!\n", rc );

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config %s\n", cfg.name.c_str());
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

  // Reserve space for intermediate output
  intermediate.clear();
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

  // Pass 2: Process matches and compute flags (store in flags_out)
  int64_t cumulative_delta = 0;
  PCRE2_SIZE next_valid_int_pos = 0;

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

    // Calculate context for flag record
    size_t ctx_before = (int_pos >= (size_t)CTX_BEFORE) ? (size_t)CTX_BEFORE : int_pos;
    size_t remaining_after = intermediate.length() - int_pos - match_len;
    size_t ctx_after = (remaining_after >= (size_t)CTX_AFTER) ? (size_t)CTX_AFTER : remaining_after;
    const char* context = intermediate.c_str() + int_pos - ctx_before;
    int ctx_ofs = (int)ctx_before;
    int ctx_len = (int)(ctx_before + match_len + ctx_after);

    FlagRecord rec;
    rec.flag = should ? 1 : 0;
    rec.context.assign(context, ctx_len);
    rec.ctx_ofs = ctx_ofs;
    rec.ctx_len = ctx_len;
    rec.match_len = (int)match_len;
    flags_out.push_back(rec);

    if (should) {
      // Update cumulative delta: we're replacing match_len with repl.length()
      cumulative_delta += (int64_t)repl.length() - (int64_t)match_len;
      // Skip all matches that start before int_pos + match_len
      next_valid_int_pos = int_end;
    }
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);
}

// Decompress with a single config - works on in-memory data
// Reads flags from API, modifies data in-place
// Returns the number of flags consumed
qword decompress_single(const ParsedConfig& cfg, string& data) {
  const string& lb = cfg.lb;
  const string& la = cfg.la;
  const vector<ReplacementPair>& pairs = cfg.pairs;
  unordered_map<string_view, string_view> backward;
  vector<string_view> backward_keys;
  pcre2_code *bwd_re;
  pcre2_match_data *match_data;
  int rc;
  PCRE2_SIZE *ovector;
  PCRE2_SIZE offset;
  string bwd_pattern;
  size_t i;

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config %s\n", cfg.name.c_str());
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

  // Apply replacements using flags, build output string
  string output;
  output.reserve(data.length() * 2);

  offset = 0;
  qword last_end = 0;
  qword flag_count = 0;
  vector<char> seen_pos(data.length(), 0);

  while (offset < data.length()) {
    rc = pcre2_jit_match(bwd_re, (PCRE2_SPTR)data.c_str(), data.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE pos, end;
    pos = ovector[0];
    end = ovector[1];

    bool should_replace = false;

    if (!seen_pos[pos]) {
      seen_pos[pos] = true;

      // Calculate context for API call
      PCRE2_SIZE match_len = end - pos;
      size_t ctx_before = (pos >= (size_t)CTX_BEFORE) ? (size_t)CTX_BEFORE : pos;
      size_t remaining_after = data.length() - pos - match_len;
      size_t ctx_after = (remaining_after >= (size_t)CTX_AFTER) ? (size_t)CTX_AFTER : remaining_after;
      const char* context = data.c_str() + pos - ctx_before;
      int ctx_ofs = (int)ctx_before;
      int ctx_len = (int)(ctx_before + match_len + ctx_after);

      int c = API(-3, context, ctx_ofs, ctx_len, (int)match_len);
      if (c != -1) {
        should_replace = (c == 1);
        flag_count++;
      }
    }

    if (should_replace) {
      // Write unmatched portion before this match
      if (pos > last_end) {
        output.append(data.c_str() + last_end, pos - last_end);
      }

      // Write replacement
      string_view match_str(data.data() + pos, end - pos);
      string_view repl = backward[match_str];
      output.append(repl.data(), repl.length());

      last_end = end;
      offset = end;
    } else {
      // Don't replace, continue from next position
      offset = pos + 1;
    }
  }

  // Write remaining portion
  if (last_end < data.length()) {
    output.append(data.c_str() + last_end, data.length() - last_end);
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);

  data = std::move(output);
  return flag_count;
}

// Compress mode - works on in-memory data, handles list mode
// API(-1) must be called before this function, API(-2) after
uint mode_compress(const vector<ParsedConfig>& configs, string& data, const char* flg_file ) {
  // For list mode, we need to:
  // 1. Apply transformations in forward order (configs[0], configs[1], ...)
  // 2. Collect flags for each config
  // 3. Write flags to API in reverse config order (for decompression)

  vector<vector<FlagRecord>> all_flags(configs.size());
  string current = data;
  string intermediate;

  // Process configs in forward order
  for (size_t i = 0; i < configs.size(); i++) {
    qword size_before = current.length();
    compress_single(configs[i], current, intermediate, all_flags[i]);
    current = std::move(intermediate);
    fprintf(stderr, "Config %s: %llu -> %llu bytes, %llu flags\n",
            configs[i].name.c_str(), size_before, (qword)current.length(), (qword)all_flags[i].size());
  }

  // Initialize API for encoding (write mode)
  if( API(-1, flg_file, 0, 0, 0)!=0 ) return 1; // error

  // Calculate total flags for progress reporting
  qword total_flag_count = 0;
  for (size_t i = 0; i < all_flags.size(); i++) {
    total_flag_count += all_flags[i].size();
  }

  // Write flags in reverse config order (for decompression which processes in reverse)
  qword flags_written = 0;
  int last_percent = -1;
  for (int i = (int)configs.size() - 1; i >= 0; i--) {
    for (size_t j = 0; j < all_flags[i].size(); j++) {
      const FlagRecord& rec = all_flags[i][j];
      API(rec.flag, rec.context.c_str(), rec.ctx_ofs, rec.ctx_len, rec.match_len);
      flags_written++;

      // Progress reporting
      int percent = (total_flag_count > 0) ? (int)(flags_written * 100 / total_flag_count) : 100;
      if (percent != last_percent) {
        fprintf(stderr, "\rWriting flags: %d%%", percent);
        fflush(stderr);
        last_percent = percent;
      }
    }
  }
  fprintf(stderr, "\r                    \r");  // Clear progress line

  API(-2, nullptr, 0, 0, 0);  // Close flags file

  data = std::move(current);
  fprintf(stderr, "Total flags: %llu\n", (qword)flags_written);

  return 0;
}

// Decompress mode - works on in-memory data, handles list mode
// API(-1) must be called before this function, API(-2) after
void mode_decompress(const vector<ParsedConfig>& configs, string& data) {
  qword total_flags = 0;
  // Process configs in reverse order
  for (int i = (int)configs.size() - 1; i >= 0; i--) {
    qword len_before = data.length();
    qword flag_count = decompress_single(configs[i], data);
    fprintf(stderr, "Config %s: %llu -> %llu bytes, %llu flags\n",
            configs[i].name.c_str(), (qword)len_before, (qword)data.length(), flag_count);
    total_flags += flag_count;
  }
  fprintf(stderr, "Total flags: %llu\n", total_flags);
}

int main(int argc, char **argv) {
  if (argc < 6 || argc > 7) {
    fprintf(stderr,
            "Usage: %s <mode> <config> <input> <output> <flags> [dll]\n"
            "Modes:\n"
            "  c - compress (forward replacement with flag generation)\n"
            "  d - decompress (reverse replacement using flags)\n"
            "Arguments:\n"
            "  config - config file, or @listfile for a list of configs\n"
            "  dll - optional: DLL/SO module name (default: default.dll)\n"
            "Examples:\n"
            "  %s c book1.cfg book1 book1.out book1.flg\n"
            "  %s d book1.cfg book1.out book1.rst book1.flg\n"
            "  %s c @list1 book1 book1.out book1.flg\n"
            "  %s d @list1 book1.out book1.rst book1.flg\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  // Determine DLL name: argv[6] if provided, otherwise "./default.dll"
  // Note: On Linux, dlopen doesn't search current directory by default
#ifdef _WIN32
  const char* default_dll = "default.dll";
#else
  const char* default_dll = "./default.dll";
#endif
  const char* dll_name = (argc >= 7) ? argv[6] : default_dll;

  // Load DLL
  if (!load_dll(dll_name)) {
    return 1;
  }

  const char* mode = argv[1];
  const char* config_arg = argv[2];
  const char* in_file = argv[3];
  const char* out_file = argv[4];
  const char* flg_file = argv[5];

  // Parse config argument - check for @ prefix for list mode
  // Load and parse all configs into memory upfront
  vector<ParsedConfig> configs;
  if (config_arg[0] == '@') {
    // List mode - parse list file and load all configs
    configs = parse_list_file(config_arg + 1);
    if (configs.empty()) {
      fprintf(stderr, "No config files found in list %s\n", config_arg + 1);
      unload_dll();
      return 1;
    }
    fprintf(stderr, "List mode: %llu configs\n", (qword)configs.size());
  } else {
    // Single config mode - load the config(s) from file
    // Note: a single file may contain multiple configs separated by empty lines
    configs = load_single_config(config_arg);
    if (configs.empty()) {
      fprintf(stderr, "No configs found in file %s\n", config_arg);
      unload_dll();
      return 1;
    }
  }

  // Read input file once
  string data = read_file(in_file);
  qword original_size = data.length();
  fprintf(stderr, "Input: %llu bytes\n", (qword)original_size);

  int result = 0;
  if (strcmp(mode, "c") == 0) {
    if( mode_compress(configs, data, flg_file)!=0 ) {
      unload_dll();
      return 1;
    }
  } else if (strcmp(mode, "d") == 0) {
    // Initialize API for decoding (read mode)
    if (API(-1, flg_file, 1, 0, 0) != 0) {
      unload_dll();
      return 1;
    }
    mode_decompress(configs, data);
    API(-2, nullptr, 0, 0, 0);  // Close flags file
  } else {
    fprintf(stderr, "Invalid mode '%s'. Use 'c' or 'd'.\n", mode);
    result = 1;
  }

  if (result == 0) {
    // Write output file once
    FILE *out = fopen(out_file, "wb");
    if (!out) {
      fprintf(stderr, "Cannot open %s for writing\n", out_file);
      result = 1;
    } else {
      fwrite(data.c_str(), 1, data.length(), out);
      fclose(out);
      fprintf(stderr, "Output: %llu bytes\n", (qword)data.length());
    }
  }

  // Unload DLL
  unload_dll();

  return result;
}


// DLL loading headers
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// DLL handle
#ifdef _WIN32
static HMODULE dll_handle = nullptr;
char* GetErrorText( void ) {
  wchar_t* lpMsgBuf;
  DWORD dw = GetLastError();
  FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, NULL
  );
  static char out[32768];
  int wl=wcslen(lpMsgBuf);
  WideCharToMultiByte( CP_OEMCP, 0, lpMsgBuf,wl+1, out,sizeof(out), 0,0 );
  LocalFree(lpMsgBuf);
  wl = strlen(out);
  for( --wl; wl>=0; wl-- ) if( (byte&)out[wl]<32 ) out[wl]=' ';
  return out;
}
#else
static void* dll_handle = nullptr;
#endif

// Load DLL and get API function
static bool load_dll(const char* dll_name) {
#ifdef _WIN32
  dll_handle = LoadLibraryA(dll_name);
  if (!dll_handle) {
    char* etxt = GetErrorText();
    fprintf(stderr, "Cannot load DLL: %s (error %lu: %s)\n", dll_name, GetLastError(), etxt);
    return false;
  }
  API = (API_func)GetProcAddress(dll_handle, "API");
  if (!API) {
    fprintf(stderr, "Cannot find API function in DLL: %s (error %lu)\n", dll_name, GetLastError());
    FreeLibrary(dll_handle);
    dll_handle = nullptr;
    return false;
  }
#else
  dll_handle = dlopen(dll_name, RTLD_NOW);
  if (!dll_handle) {
    fprintf(stderr, "Cannot load DLL: %s (%s)\n", dll_name, dlerror());
    return false;
  }
  API = (API_func)dlsym(dll_handle, "API");
  if (!API) {
    fprintf(stderr, "Cannot find API function in DLL: %s (%s)\n", dll_name, dlerror());
    dlclose(dll_handle);
    dll_handle = nullptr;
    return false;
  }
#endif
  return true;
}

// Unload DLL
static void unload_dll() {
  if (dll_handle) {
#ifdef _WIN32
    FreeLibrary(dll_handle);
#else
    dlclose(dll_handle);
#endif
    dll_handle = nullptr;
  }
  API = nullptr;
}
