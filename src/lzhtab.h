// lzhtab.h - LZH1 tables shared by the encoder and decoder (internal; format in lzh.h).
#ifndef S98_LZHTAB_H
#define S98_LZHTAB_H
#include "s98types.h"

enum {
  LZ_NLEN = 29,          // length codes 257..285
  LZ_NLITLEN = 286,      // 256 literals + end of block + 29 length codes
  LZ_NDIST = 32,
  LZ_WINDOW = 65535,     // largest distance
  LZ_MINMATCH = 3,
  LZ_MAXMATCH = 258
};

extern const u16 g_lz_lbase[LZ_NLEN];
extern const u8 g_lz_lext[LZ_NLEN];
extern const u16 g_lz_dbase[LZ_NDIST];
extern const u8 g_lz_dext[LZ_NDIST];

#endif
