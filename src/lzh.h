// lzh.h - Setup98 codec "LZH1": LZ77 (64 KB window, matches 3..258) + canonical Huffman, our own format.
//
// Stream = blocks, bits packed LSB first. Block:
//   1 bit   last block
//   lengths of the 286 literal/length codes, then of the 32 distance codes, 4 bits each (1..15); a 0 nibble is
//           followed by 4 bits r and means r+1 zero lengths
//   symbols (Huffman codes sent MSB first, i.e. bit-reversed), 256 = end of block
// Literal/length alphabet: 0..255 literals, 256 end of block, 257..285 lengths 3..258 (deflate's base/extra table).
// Distance alphabet: deflate's 30 codes + 30 = 32769.., 31 = 49153.. (14 extra bits each), maximum distance 65535.
// Code lengths are limited to 15 bits.
//
// Filters run before compression and are undone after decompression (one pass, in place):
//   F_D1..F_D4  byte delta with stride 1..4 (24-bit BMP: stride 3)
//   F_W1, F_W2  16-bit little-endian sample delta, stride 1 or 2 samples (16-bit mono / stereo WAV)
#ifndef S98_LZH_H
#define S98_LZH_H
#include "s98types.h"

enum { LZH_OK = 0, LZH_ECORRUPT = -1, LZH_ENOMEM = -2 };
enum { F_NONE = 0, F_D1, F_D2, F_D3, F_D4, F_W1, F_W2, F_COUNT };

// compressed size of n bytes never exceeds this when lzh_compress succeeds (callers may pass less: then a stream
// that would not fit fails with 0 and the caller stores the data instead)
u32 lzh_bound(u32 n);

// returns the compressed size, 0 if it doesn't fit into cap (or n == 0), or (u32)-1 when memory runs out.
// Deterministic: the same input gives the same bytes on every compiler and host.
u32 lzh_compress(const u8* in, u32 n, u8* out, u32 cap);

// decodes exactly outn bytes; LZH_OK, or LZH_ECORRUPT for any malformed, truncated or oversized stream.
// Never reads past in[n-1] or writes past out[outn-1]. Uses about 6 KB of stack, no heap.
int lzh_decompress(const u8* in, u32 n, u8* out, u32 outn);

// filters (see above); filt_apply writes n bytes to out (out != in), filt_undo works in place
void filt_apply(int f, const u8* in, u8* out, u32 n);
void filt_undo(int f, u8* buf, u32 n);
const char* filt_name(int f);

#endif
