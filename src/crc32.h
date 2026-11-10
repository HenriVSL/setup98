// crc32.h - CRC-32 (IEEE 802.3, the zip/PNG polynomial 0xEDB88320, reflected, init/xor 0xFFFFFFFF).
#ifndef S98_CRC32_H
#define S98_CRC32_H
#include "s98types.h"

// crc32_update(0, data, n) = CRC of data; feed chunks by passing the previous result
u32 crc32_update(u32 crc, const u8* data, u32 n);

#endif
