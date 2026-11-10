// s98types.h - Setup98 basic types. Portable C++98: builds with VC6 (Win98) and clang (Mac).
// u32 is 32 bits on both (int is 32 bits on ILP32 Win32 and LP64 macOS; never use long for sizes).
#ifndef S98_TYPES_H
#define S98_TYPES_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#include "version.h"

// little-endian helpers (byte-wise: the format is identical on every host)
inline u32 get_le32(const u8* p)
{
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

inline void put_le32(u8* p, u32 v)
{
  p[0] = (u8)(v & 0xFF);
  p[1] = (u8)((v >> 8) & 0xFF);
  p[2] = (u8)((v >> 16) & 0xFF);
  p[3] = (u8)((v >> 24) & 0xFF);
}

#endif
