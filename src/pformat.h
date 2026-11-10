// pformat.h - Setup98 payload format: what pack98 appends to setup98.exe and what the stub reads back.
// All integers little-endian, written byte by byte (identical on every host).
//
//   [stub exe bytes]                        payload_offset = size of the stub
//   payload:
//     header   16 bytes   "S98H", u32 manifest_size, u32 nfiles, u32 toc_size
//     manifest            canonical manifest text (ASCII, CRLF), manifest_size bytes
//     toc                 nfiles entries, toc_size bytes in all:
//                           u32 usize, u32 csize, u32 crc32 (of the original bytes), u32 offset (in the data area),
//                           u8 method (0 stored, 1 LZH1), u8 filter (F_*), u8 flags (PF_*), u8 namelen, name
//                           (relative install path, '\' separators, ASCII, no terminator)
//     data                file data, each at its offset
//   trailer  32 bytes (the very end of the file):
//     "Setup98\x1A", u32 format (1), u32 payload_offset, u32 payload_size, u32 payload_crc32,
//     u32 data_offset (= 16 + manifest_size + toc_size, relative to the payload), u32 crc32 of the first 28 bytes
//
// An uninstaller (uninst.exe) is the stub + a payload with nfiles = 0: the manifest only.
#ifndef S98_PFORMAT_H
#define S98_PFORMAT_H
#include "s98types.h"

#define PF_MAGIC "Setup98\x1A"
#define PF_HMAGIC "S98H"
enum {
  PF_FORMAT = 1,
  PF_TRAILER = 32,
  PF_HEADER = 16,
  PF_ENTRY_FIXED = 20,        // bytes before the name
  PF_KEEP = 1,                // flags: installed only if missing (a default config the user may edit)
  PF_M_STORED = 0,
  PF_M_LZH = 1,
  PF_MAX_FILES = 20000,
  PF_MAX_NAME = 200,          // longest relative path (bytes)
  PF_MAX_FILE = 0x10000000    // largest single file: 256 MB
};

struct PfTrailer {
  u32 format, payload_offset, payload_size, payload_crc, data_offset;
};

struct PfEntry {
  u32 usize, csize, crc, offset;
  u8 method, filter, flags;
  char name[PF_MAX_NAME + 1];
};

// parses the 32 trailer bytes; 1 = valid
inline int pf_parse_trailer(const u8* t, PfTrailer* out);
// writes the 32 trailer bytes
inline void pf_make_trailer(u8* t, const PfTrailer* in);
// reads TOC entry i starting at *pos inside toc[0..toc_size); 1 = ok (pos advanced)
inline int pf_read_entry(const u8* toc, u32 toc_size, u32* pos, PfEntry* e);

// ---- inline implementations (header-only so the stub and pack98 share them) ----
#include "crc32.h"

inline int pf_parse_trailer(const u8* t, PfTrailer* out)
{
  int i;
  for (i = 0; i < 8; i++) if (t[i] != (u8)PF_MAGIC[i]) return 0;
  if (crc32_update(0, t, 28) != get_le32(t + 28)) return 0;
  out->format = get_le32(t + 8);
  out->payload_offset = get_le32(t + 12);
  out->payload_size = get_le32(t + 16);
  out->payload_crc = get_le32(t + 20);
  out->data_offset = get_le32(t + 24);
  if (out->format != PF_FORMAT || out->data_offset < PF_HEADER || out->data_offset > out->payload_size) return 0;
  return 1;
}

inline void pf_make_trailer(u8* t, const PfTrailer* in)
{
  int i;
  for (i = 0; i < 8; i++) t[i] = (u8)PF_MAGIC[i];
  put_le32(t + 8, in->format);
  put_le32(t + 12, in->payload_offset);
  put_le32(t + 16, in->payload_size);
  put_le32(t + 20, in->payload_crc);
  put_le32(t + 24, in->data_offset);
  put_le32(t + 28, crc32_update(0, t, 28));
}

inline int pf_read_entry(const u8* toc, u32 toc_size, u32* pos, PfEntry* e)
{
  u32 p = *pos, nl, i;
  if (p > toc_size || toc_size - p < PF_ENTRY_FIXED) return 0;
  e->usize = get_le32(toc + p);
  e->csize = get_le32(toc + p + 4);
  e->crc = get_le32(toc + p + 8);
  e->offset = get_le32(toc + p + 12);
  e->method = toc[p + 16];
  e->filter = toc[p + 17];
  e->flags = toc[p + 18];
  nl = toc[p + 19];
  p += PF_ENTRY_FIXED;
  if (nl == 0 || nl > PF_MAX_NAME || toc_size - p < nl) return 0;
  for (i = 0; i < nl; i++) {
    char c = (char)toc[p + i];
    if (c < 32 || c > 126) return 0;
    e->name[i] = c;
  }
  e->name[nl] = 0;
  if (e->method > PF_M_LZH || e->usize > PF_MAX_FILE || (e->method == PF_M_STORED && e->csize != e->usize)) return 0;
  *pos = p + nl;
  return 1;
}

#endif
