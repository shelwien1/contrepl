// DLL module containing API function for repl2
// Compile on Linux: g++ -shared -fPIC -o default.dll default_dll.cpp
// Compile on Windows: cl /LD default_dll.cpp /Fe:default.dll

#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT __attribute__((visibility("default")))
#endif

// Context size constants for API
static const int CTX_BEFORE = 32;  // symbols before match
static const int CTX_AFTER = 32;   // symbols after match

// Debug mode for API
static const int API_DEBUG = 1;  // set to 0 to disable debug logging

// API function for flags I/O
// bit=-1: constructor, ctx=filename, ofs=mode (0=encode/write, 1=decode/read)
// bit=-2: destructor
// bit=-3: read flag (decode mode), returns 0/1 or -1 on EOF
// bit>=0: write flag (encode mode)
static FILE* api_flg = nullptr;
static FILE* api_dbg = nullptr;
static int api_mode = 0;  // 0=encode, 1=decode

static void api_log(int flag, int ofs, int len, int mlen, const char* ctx) {
  if (!api_dbg || !ctx) return;
  fprintf(api_dbg, "%d %d %d %d ", flag, ofs, len, mlen);
  for (int i = 0; i < len; i++) {
    fprintf(api_dbg, "%02X", (unsigned char)ctx[i]);
  }
  fprintf(api_dbg, "\n");
}

extern "C" DLLEXPORT int API(char bit, const char* ctx, int ofs, int len, int mlen) {
  if (bit == -1) {
    // Constructor: open flags file
    api_mode = ofs;
    if (api_mode == 0) {
      // Encode mode - open for writing
      api_flg = fopen(ctx, "wb");
      if (API_DEBUG) api_dbg = fopen("dbg_c.log", "w");
    } else {
      // Decode mode - open for reading
      api_flg = fopen(ctx, "rb");
      if (API_DEBUG) api_dbg = fopen("dbg_d.log", "w");
    }
    if (!api_flg) {
      fprintf(stderr, "Cannot open flags file %s\n", ctx);
      return 1;  // error
    }
    return 0;  // success
  } else if (bit == -2) {
    // Destructor: close files
    if (api_flg) {
      fclose(api_flg);
      api_flg = nullptr;
    }
    if (api_dbg) {
      fclose(api_dbg);
      api_dbg = nullptr;
    }
    return 0;
  } else if (bit == -3) {
    // Read flag (decode mode)
    if (!api_flg) return -1;
    int c = fgetc(api_flg);
    if (c == EOF) return -1;
    int flag = (c == '1') ? 1 : 0;
    if (API_DEBUG) api_log(flag, ofs, len, mlen, ctx);
    return flag;
  } else {
    // Write flag (encode mode)
    if (!api_flg) return -1;
    int flag = bit ? 1 : 0;
    fputc(flag ? '1' : '0', api_flg);
    if (API_DEBUG) api_log(flag, ofs, len, mlen, ctx);
    return 0;
  }
}
