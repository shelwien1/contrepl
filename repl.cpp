#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <vector>
#include <string>
#include <map>

using namespace std;

typedef unsigned long long qword;
typedef unsigned int uint;
typedef unsigned short word;
typedef unsigned char byte;

struct ReplacementPair {
  string from;
  string to;
};

bool parse_hex_char(const char* hex, ::byte* result) {
  int val;
  val = 0;
  if (sscanf(hex, "%2x", &val) != 1) {
    return false;
  }
  *result = (::byte)val;
  return true;
}

bool parse_config(const char* config_file, string& lookbehind,
                  string& lookahead,
                  vector<ReplacementPair>& pairs) {
  FILE* f;
  char line[8192];
  size_t i, len;
  string left, right;
  ReplacementPair pair;
  bool in_left;
  ::byte hex_val;

  f = fopen(config_file, "rb");
  if (!f) {
    fprintf(stderr,
      "Error: Cannot open config file: %s\n",
      config_file);
    return false;
  }

  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    fprintf(stderr, "Error: Cannot read lookbehind\n");
    return false;
  }
  len = strlen(line);
  if (len > 0 && line[len-1] == '\n') {
    line[len-1] = 0;
    len--;
  }
  if (len > 0 && line[len-1] == '\r') {
    line[len-1] = 0;
    len--;
  }
  lookbehind = line;

  if (!fgets(line, sizeof(line), f)) {
    fclose(f);
    fprintf(stderr, "Error: Cannot read lookahead\n");
    return false;
  }
  len = strlen(line);
  if (len > 0 && line[len-1] == '\n') {
    line[len-1] = 0;
    len--;
  }
  if (len > 0 && line[len-1] == '\r') {
    line[len-1] = 0;
    len--;
  }
  lookahead = line;

  while (fgets(line, sizeof(line), f)) {
    len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = 0;
      len--;
    }
    if (len > 0 && line[len-1] == '\r') {
      line[len-1] = 0;
      len--;
    }
    if (len == 0) continue;

    left.clear();
    right.clear();
    in_left = true;
    i = 0;

    while (i < len) {
      if (line[i] == '\t') {
        in_left = false;
        i++;
        continue;
      }

      if (line[i] == '\\' && i+3 < len &&
          line[i+1] == 'x') {
        if (parse_hex_char(&line[i+2], &hex_val)) {
          if (in_left) {
            left += (char)hex_val;
          } else {
            right += (char)hex_val;
          }
          i += 4;
        } else {
          if (in_left) {
            left += line[i];
          } else {
            right += line[i];
          }
          i++;
        }
      } else {
        if (in_left) {
          left += line[i];
        } else {
          right += line[i];
        }
        i++;
      }
    }

    pair.from = left;
    pair.to = right;
    pairs.push_back(pair);
  }

  fclose(f);
  return true;
}

bool process_file(const char* input_file,
                  const char* output_file,
                  const string& lookbehind,
                  const string& lookahead,
                  const vector<ReplacementPair>& pairs,
                  bool decode) {
  FILE *fin, *fout;
  vector<::byte> input_data;
  vector<::byte> output_data;
  ::byte buffer[8192];
  size_t n, i, j, pair_idx;
  pcre2_code* re;
  pcre2_match_data* match_data;
  PCRE2_SIZE* ovector;
  int rc;
  string pattern;
  int errornumber;
  PCRE2_SIZE erroroffset;
  char hex[3];
  qword file_size;
  map<string, string> replacement_map;
  string matched_str;
  size_t pos;
  PCRE2_SIZE match_len;

  fin = fopen(input_file, "rb");
  if (!fin) {
    fprintf(stderr,
      "Error: Cannot open input file: %s\n",
      input_file);
    return false;
  }

  file_size = 0;
  while (1) {
    n = fread(buffer, 1, sizeof(buffer), fin);
    if (n == 0) break;
    file_size += n;
    i = 0;
    while (i < n) {
      input_data.push_back(buffer[i]);
      i++;
    }
  }
  fclose(fin);

  fprintf(stderr, "Read %lld bytes from input\n", file_size);

  if (lookbehind.length() > 0) {
    pattern = "(?<=";
    pattern += lookbehind;
    pattern += ")";
  } else {
    pattern = "";
  }
  pattern += "(";

  pair_idx = 0;
  while (pair_idx < pairs.size()) {
    if (pair_idx > 0) {
      pattern += "|";
    }

    j = 0;
    if (decode) {
      replacement_map[pairs[pair_idx].to] = pairs[pair_idx].from;
      while (j < pairs[pair_idx].to.length()) {
        pattern += "\\x";
        sprintf(hex, "%02x", (::byte)pairs[pair_idx].to[j]);
        pattern += hex;
        j++;
      }
    } else {
      replacement_map[pairs[pair_idx].from] = pairs[pair_idx].to;
      while (j < pairs[pair_idx].from.length()) {
        pattern += "\\x";
        sprintf(hex, "%02x", (::byte)pairs[pair_idx].from[j]);
        pattern += hex;
        j++;
      }
    }
    pair_idx++;
  }

  pattern += ")";
  if (lookahead.length() > 0) {
    pattern += "(?=";
    pattern += lookahead;
    pattern += ")";
  }

  re = pcre2_compile((PCRE2_SPTR)pattern.c_str(),
                     PCRE2_ZERO_TERMINATED,
                     0, &errornumber, &erroroffset, NULL);
  if (!re) {
    fprintf(stderr, "Error: PCRE2 compilation failed\n");
    return false;
  }

  match_data = pcre2_match_data_create_from_pattern(re, NULL);

  pos = 0;
  while (pos < input_data.size()) {
    rc = pcre2_match(re,
                     (PCRE2_SPTR)input_data.data(),
                     input_data.size(), pos, 0,
                     match_data, NULL);

    if (rc < 0) {
      j = pos;
      while (j < input_data.size()) {
        output_data.push_back(input_data[j]);
        j++;
      }
      break;
    }

    ovector = pcre2_get_ovector_pointer(match_data);

    j = pos;
    while (j < ovector[2]) {
      output_data.push_back(input_data[j]);
      j++;
    }

    matched_str.clear();
    match_len = ovector[3] - ovector[2];
    j = 0;
    while (j < match_len) {
      matched_str += (char)input_data[ovector[2] + j];
      j++;
    }

    if (replacement_map.find(matched_str) !=
        replacement_map.end()) {
      j = 0;
      while (j < replacement_map[matched_str].length()) {
        output_data.push_back(replacement_map[matched_str][j]);
        j++;
      }
    } else {
      j = 0;
      while (j < matched_str.length()) {
        output_data.push_back(matched_str[j]);
        j++;
      }
    }

    pos = ovector[3];
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(re);

  fout = fopen(output_file, "wb");
  if (!fout) {
    fprintf(stderr,
      "Error: Cannot open output file: %s\n",
      output_file);
    return false;
  }

  if (output_data.size() > 0) {
    fwrite(output_data.data(), 1, output_data.size(), fout);
  }
  fclose(fout);

  fprintf(stderr,
    "Wrote %lld bytes to output\n",
    (qword)output_data.size());

  return true;
}

int main(int argc, char* argv[]) {
  string lookbehind, lookahead;
  vector<ReplacementPair> pairs;
  bool decode;

  if (argc != 5) {
    fprintf(stderr,
      "Usage:\n"
      "  %s c config.txt inputfile outputfile\n"
      "  %s d config.txt outputfile restoredfile\n",
      argv[0], argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "c") == 0) {
    decode = false;
  } else if (strcmp(argv[1], "d") == 0) {
    decode = true;
  } else {
    fprintf(stderr, "Error: Mode must be 'c' or 'd'\n");
    return 1;
  }

  if (!parse_config(argv[2], lookbehind, lookahead, pairs)) {
    fprintf(stderr, "Error: Failed to parse config\n");
    return 1;
  }

  fprintf(stderr,
    "Loaded %d replacement pairs\n",
    (int)pairs.size());

  if (!process_file(argv[3], argv[4], lookbehind, lookahead,
                    pairs, decode)) {
    fprintf(stderr, "Error: Failed to process file\n");
    return 1;
  }

  return 0;
}
