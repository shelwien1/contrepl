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

bool process_flags(const char* input_file,
                   const char* flag_file,
                   const string& lookbehind,
                   const string& lookahead,
                   const vector<ReplacementPair>& pairs,
                   bool decode) {
  FILE *fin, *fout;
  vector<::byte> input_data;
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
  qword file_size, flags_written;
  map<string, string> replacement_map;
  string matched_str;
  size_t pos;
  PCRE2_SIZE match_len;
  char flag;

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

  fout = fopen(flag_file, "wb");
  if (!fout) {
    fprintf(stderr,
      "Error: Cannot open flag file: %s\n",
      flag_file);
    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
    return false;
  }

  flags_written = 0;
  pos = 0;
  while (pos < input_data.size()) {
    rc = pcre2_match(re,
                     (PCRE2_SPTR)input_data.data(),
                     input_data.size(), pos, 0,
                     match_data, NULL);

    if (rc < 0) {
      break;
    }

    ovector = pcre2_get_ovector_pointer(match_data);

    matched_str.clear();
    match_len = ovector[3] - ovector[2];
    j = 0;
    while (j < match_len) {
      matched_str += (char)input_data[ovector[2] + j];
      j++;
    }

    if (replacement_map.find(matched_str) !=
        replacement_map.end()) {
      flag = '1';
    } else {
      flag = '0';
    }

    fputc(flag, fout);
    flags_written++;

    pos = ovector[3];
  }

  fclose(fout);
  pcre2_match_data_free(match_data);
  pcre2_code_free(re);

  fprintf(stderr,
    "Wrote %lld flags to %s\n",
    flags_written,
    flag_file);

  return true;
}

bool compare_flags(const char* fwd_file,
                   const char* bwd_file,
                   const char* dif_file) {
  FILE *ffwd, *fbwd, *fdif;
  int c, d;
  qword pos;
  bool error;

  ffwd = fopen(fwd_file, "rb");
  if (!ffwd) {
    fprintf(stderr,
      "Error: Cannot open forward file: %s\n",
      fwd_file);
    return false;
  }

  fbwd = fopen(bwd_file, "rb");
  if (!fbwd) {
    fclose(ffwd);
    fprintf(stderr,
      "Error: Cannot open backward file: %s\n",
      bwd_file);
    return false;
  }

  fdif = fopen(dif_file, "wb");
  if (!fdif) {
    fclose(ffwd);
    fclose(fbwd);
    fprintf(stderr,
      "Error: Cannot open diff file: %s\n",
      dif_file);
    return false;
  }

  pos = 0;
  error = false;
  while (1) {
    c = fgetc(ffwd);
    d = fgetc(fbwd);
    if ((c == EOF) || (d == EOF)) break;

    if (d == '1') {
      fputc(c, fdif);
    }
    if ((d == '0') && (c == '1')) {
      fprintf(stderr,
        "Error: missing match in backward log.\n");
      error = true;
      break;
    }
    pos++;
  }

  if (c + d != -2) {
    fprintf(stderr,
      "Error: length of fwd- and bwd- files isn't the same.\n");
    error = true;
  }

  fclose(ffwd);
  fclose(fbwd);
  fclose(fdif);

  if (!error) {
    fprintf(stderr,
      "Compared %lld flags, wrote diff to %s\n",
      pos,
      dif_file);
  }

  return !error;
}

int main(int argc, char* argv[]) {
  string lookbehind, lookahead;
  vector<ReplacementPair> pairs;

  if (argc != 6) {
    fprintf(stderr,
      "Usage:\n"
      "  %s config.txt inputfile fwd-file bwd-file dif-file\n",
      argv[0]);
    return 1;
  }

  if (!parse_config(argv[1], lookbehind, lookahead, pairs)) {
    fprintf(stderr, "Error: Failed to parse config\n");
    return 1;
  }

  fprintf(stderr,
    "Loaded %d replacement pairs\n",
    (int)pairs.size());

  fprintf(stderr, "Pass 1: Forward replacements\n");
  if (!process_flags(argv[2], argv[3], lookbehind, lookahead,
                     pairs, false)) {
    fprintf(stderr, "Error: Failed forward pass\n");
    return 1;
  }

  fprintf(stderr, "Pass 2: Backward replacements\n");
  if (!process_flags(argv[2], argv[4], lookbehind, lookahead,
                     pairs, true)) {
    fprintf(stderr, "Error: Failed backward pass\n");
    return 1;
  }

  fprintf(stderr, "Pass 3: Compare and write diff\n");
  if (!compare_flags(argv[3], argv[4], argv[5])) {
    fprintf(stderr, "Error: Failed comparison\n");
    return 1;
  }

  return 0;
}
