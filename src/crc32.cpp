// crc32.cpp - table-driven CRC-32 (see crc32.h).
#include "crc32.h"

static u32 g_crc_table[256];
static int g_crc_ready = 0;

static void crc_init()
{
  u32 i, k, c;
  for (i = 0; i < 256; i++) {
    c = i;
    for (k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    g_crc_table[i] = c;
  }
  g_crc_ready = 1;
}

u32 crc32_update(u32 crc, const u8* data, u32 n)
{
  u32 c, i;
  if (!g_crc_ready) crc_init();
  c = crc ^ 0xFFFFFFFFu;
  for (i = 0; i < n; i++) c = g_crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
