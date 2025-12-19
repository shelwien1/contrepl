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
      if (line.empty()) {
        if (!current.pairs.empty()) {
          configs.push_back(std::move(current));
          config_index++;
          current = ParsedConfig();
          current.name = base_name + "[" + to_string(config_index) + "]";
          line_num = -1;
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
      }
    }

    line_start = line_end + 1;
    line_num++;
  }

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

  // Sort by length descending
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

// Forward replacement: replace all 'from' with 'to'
void replace_forward(const ParsedConfig& cfg, const string& input, string& output) {
  const string& lb = cfg.lb;
  const string& la = cfg.la;
  const vector<ReplacementPair>& pairs = cfg.pairs;
  unordered_map<string_view, string_view> forward;
  vector<string_view> forward_keys;
  pcre2_code *fwd_re;
  pcre2_match_data *match_data;
  int rc;
  PCRE2_SIZE *ovector;
  PCRE2_SIZE offset;
  string fwd_pattern;

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config %s\n", cfg.name.c_str());
    exit(1);
  }

  forward_keys.reserve(pairs.size());

  // Build forward map
  for (size_t i = 0; i < pairs.size(); i++) {
    string_view from_view(pairs[i].from);
    string_view to_view(pairs[i].to);
    forward[from_view] = to_view;
    forward_keys.push_back(from_view);
  }

  output.clear();
  output.reserve(input.length());

  // Build forward regex pattern
  fwd_pattern = "(?<=" + lb + ")(" + build_alternation(forward_keys) + ")(?=" + la + ")";

  int errcode;
  PCRE2_SIZE erroffset;
  fwd_re = pcre2_compile((PCRE2_SPTR)fwd_pattern.c_str(), PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  if (!fwd_re) {
    fprintf(stderr, "PCRE2 compilation failed\n");
    exit(1);
  }
  pcre2_jit_compile(fwd_re, PCRE2_JIT_COMPLETE);

  match_data = pcre2_match_data_create_from_pattern(fwd_re, NULL);

  // Forward replacement
  offset = 0;
  qword last_end = 0;
  qword replace_count = 0;

  while (offset < input.length()) {
    rc = pcre2_jit_match(fwd_re, (PCRE2_SPTR)input.c_str(), input.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start = ovector[0];
    PCRE2_SIZE end = ovector[1];

    // Add unmatched portion
    if (start > last_end) {
      output.append(input.data() + last_end, start - last_end);
    }

    // Add replacement
    string_view match_str(input.data() + start, end - start);
    string_view repl = forward[match_str];
    output.append(repl.data(), repl.length());

    replace_count++;
    last_end = end;
    offset = end;
    if (offset == start)
      offset++;
  }

  // Add remaining portion
  if (last_end < input.length()) {
    output.append(input.data() + last_end, input.length() - last_end);
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(fwd_re);

  fprintf(stderr, "Config %s: %llu replacements\n", cfg.name.c_str(), replace_count);
}

// Backward replacement: replace all 'to' with 'from'
void replace_backward(const ParsedConfig& cfg, const string& input, string& output) {
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

  if (pairs.empty()) {
    fprintf(stderr, "No replacement pairs found in config %s\n", cfg.name.c_str());
    exit(1);
  }

  backward_keys.reserve(pairs.size());

  // Build backward map
  for (size_t i = 0; i < pairs.size(); i++) {
    string_view from_view(pairs[i].from);
    string_view to_view(pairs[i].to);
    if (backward.find(to_view) == backward.end()) {
      backward[to_view] = from_view;
      backward_keys.push_back(to_view);
    }
  }

  output.clear();
  output.reserve(input.length() * 2);

  // Build backward regex pattern
  bwd_pattern = "(?<=" + lb + ")(" + build_alternation(backward_keys) + ")(?=" + la + ")";

  int errcode;
  PCRE2_SIZE erroffset;
  bwd_re = pcre2_compile((PCRE2_SPTR)bwd_pattern.c_str(), PCRE2_ZERO_TERMINATED, 0, &errcode, &erroffset, NULL);
  if (!bwd_re) {
    fprintf(stderr, "PCRE2 compilation failed\n");
    exit(1);
  }
  pcre2_jit_compile(bwd_re, PCRE2_JIT_COMPLETE);

  match_data = pcre2_match_data_create_from_pattern(bwd_re, NULL);

  // Backward replacement
  offset = 0;
  qword last_end = 0;
  qword replace_count = 0;

  while (offset < input.length()) {
    rc = pcre2_jit_match(bwd_re, (PCRE2_SPTR)input.c_str(), input.length(), offset, 0, match_data, NULL);

    if (rc < 0)
      break;

    ovector = pcre2_get_ovector_pointer(match_data);
    PCRE2_SIZE start = ovector[0];
    PCRE2_SIZE end = ovector[1];

    // Add unmatched portion
    if (start > last_end) {
      output.append(input.data() + last_end, start - last_end);
    }

    // Add replacement
    string_view match_str(input.data() + start, end - start);
    string_view repl = backward[match_str];
    output.append(repl.data(), repl.length());

    replace_count++;
    last_end = end;
    offset = end;
    if (offset == start)
      offset++;
  }

  // Add remaining portion
  if (last_end < input.length()) {
    output.append(input.data() + last_end, input.length() - last_end);
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(bwd_re);

  fprintf(stderr, "Config %s: %llu replacements\n", cfg.name.c_str(), replace_count);
}

// Compress mode: apply forward replacements in order
void mode_compress(const vector<ParsedConfig>& configs, string& data) {
  string current = data;
  string output;

  for (size_t i = 0; i < configs.size(); i++) {
    qword size_before = current.length();
    replace_forward(configs[i], current, output);
    fprintf(stderr, "Config %s: %llu -> %llu bytes\n",
            configs[i].name.c_str(), size_before, (qword)output.length());
    current = std::move(output);
  }

  data = std::move(current);
}

// Decompress mode: apply backward replacements in reverse order
void mode_decompress(const vector<ParsedConfig>& configs, string& data) {
  string current = data;
  string output;

  for (int i = (int)configs.size() - 1; i >= 0; i--) {
    qword size_before = current.length();
    replace_backward(configs[i], current, output);
    fprintf(stderr, "Config %s: %llu -> %llu bytes\n",
            configs[i].name.c_str(), size_before, (qword)output.length());
    current = std::move(output);
  }

  data = std::move(current);
}

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr,
            "Usage: %s <mode> <config> <input> <output>\n"
            "Modes:\n"
            "  c - compress (forward replacement: from -> to)\n"
            "  d - decompress (backward replacement: to -> from)\n"
            "Arguments:\n"
            "  config - config file, or @listfile for a list of configs\n"
            "Examples:\n"
            "  %s c book1.cfg book1 book1.out\n"
            "  %s d book1.cfg book1.out book1.rst\n"
            "  %s c @list1 book1 book1.out\n"
            "  %s d @list1 book1.out book1.rst\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 1;
  }

  const char* mode = argv[1];
  const char* config_arg = argv[2];
  const char* in_file = argv[3];
  const char* out_file = argv[4];

  // Parse config argument - check for @ prefix for list mode
  vector<ParsedConfig> configs;
  if (config_arg[0] == '@') {
    configs = parse_list_file(config_arg + 1);
    if (configs.empty()) {
      fprintf(stderr, "No config files found in list %s\n", config_arg + 1);
      return 1;
    }
    fprintf(stderr, "List mode: %llu configs\n", (qword)configs.size());
  } else {
    configs = load_single_config(config_arg);
    if (configs.empty()) {
      fprintf(stderr, "No configs found in file %s\n", config_arg);
      return 1;
    }
  }

  // Read input file
  string data = read_file(in_file);
  qword original_size = data.length();
  fprintf(stderr, "Input: %llu bytes\n", original_size);

  int result = 0;
  if (strcmp(mode, "c") == 0) {
    mode_compress(configs, data);
  } else if (strcmp(mode, "d") == 0) {
    mode_decompress(configs, data);
  } else {
    fprintf(stderr, "Invalid mode '%s'. Use 'c' or 'd'.\n", mode);
    result = 1;
  }

  if (result == 0) {
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

  return result;
}
