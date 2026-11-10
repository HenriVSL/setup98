// lzhdec.cpp - LZH1 decoder and the filters (format: lzh.h). Small and defensive: every length, distance and
// table is checked, so corrupt or hostile input returns LZH_ECORRUPT and never touches memory outside the buffers.
#include "lzh.h"
#include "lzhtab.h"

const u16 g_lz_lbase[LZ_NLEN] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99,
                                  115, 131, 163, 195, 227, 258 };
const u8 g_lz_lext[LZ_NLEN] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const u16 g_lz_dbase[LZ_NDIST] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025,
                                   1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153 };
const u8 g_lz_dext[LZ_NDIST] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
                                 12, 13, 13, 14, 14 };

u32 lzh_bound(u32 n)
{
  // worst case: every byte a 15-bit literal (+ headers of 1 block per 64 K tokens); generous
  return n + n / 2 + 1024 + (n / 65536 + 1) * 200;
}

// ---- bit reader ----

struct BitIn {
  const u8* p;
  const u8* end;
  u32 buf;
  int cnt;
  int over;      // bytes read past the end (zeros); a valid stream peeks at most a few
};

static inline void bi_need(BitIn* b, int n)
{
  while (b->cnt < n) {
    u32 c = 0;
    if (b->p < b->end) c = *b->p++;
    else b->over++;
    b->buf |= c << b->cnt;
    b->cnt += 8;
  }
}

static inline u32 bi_get(BitIn* b, int n)
{
  u32 v;
  if (n == 0) return 0;
  bi_need(b, n);
  v = b->buf & ((1u << n) - 1);
  b->buf >>= n;
  b->cnt -= n;
  return v;
}

// ---- Huffman tables ----

enum { FAST_BITS = 10 };

struct Huff {
  u16 fast[1 << FAST_BITS];      // (symbol << 4) | length for codes of <= FAST_BITS bits, 0 = use the slow path
  u16 count[16];                 // number of codes of each length
  u16 sym[LZ_NLITLEN];           // symbols ordered by length, then value
};

// builds the tables from code lengths: 1 ok, 0 no code at all, -1 over-subscribed
static int huff_build(Huff* h, const u8* len, int n)
{
  u16 offs[16];
  int i, left, total = 0;
  for (i = 0; i < 16; i++) h->count[i] = 0;
  for (i = 0; i < n; i++) h->count[len[i]]++;
  h->count[0] = 0;
  left = 1;
  for (i = 1; i < 16; i++) {
    left <<= 1;
    left -= h->count[i];
    if (left < 0) return -1;              // over-subscribed
    total += h->count[i];
  }
  if (total == 0) return 0;
  offs[1] = 0;
  for (i = 1; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->count[i]);
  for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (u16)i;
  // fast table: canonical codes, bit-reversed (the stream sends the code's top bit first)
  for (i = 0; i < (1 << FAST_BITS); i++) h->fast[i] = 0;
  {
    u32 code = 0;
    int l, k = 0;
    for (l = 1; l <= FAST_BITS; l++) {
      int c;
      for (c = 0; c < h->count[l]; c++, k++) {
        u32 r = 0, x = code;
        int b;
        for (b = 0; b < l; b++) { r = (r << 1) | (x & 1); x >>= 1; }
        for (; r < (1u << FAST_BITS); r += (1u << l)) h->fast[r] = (u16)((h->sym[k] << 4) | l);
        code++;
      }
      code <<= 1;
    }
  }
  return 1;
}

// returns the symbol or -1 (no such code)
static inline int huff_decode(BitIn* b, const Huff* h)
{
  u32 e;
  int code, first, index, l;
  bi_need(b, FAST_BITS);
  e = h->fast[b->buf & ((1u << FAST_BITS) - 1)];
  if (e) {
    int n = (int)(e & 15);
    b->buf >>= n;
    b->cnt -= n;
    return (int)(e >> 4);
  }
  code = first = index = 0;                // slow path, one bit at a time (puff style)
  for (l = 1; l < 16; l++) {
    int count;
    code |= (int)bi_get(b, 1);
    count = h->count[l];
    if (code - count < first) return h->sym[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

static int read_lengths(BitIn* b, u8* len, int n)
{
  int i = 0;
  while (i < n) {
    int v = (int)bi_get(b, 4);
    if (v) len[i++] = (u8)v;
    else {
      int run = (int)bi_get(b, 4) + 1;
      if (i + run > n) return 0;
      while (run--) len[i++] = 0;
    }
    if (b->over > 8) return 0;
  }
  return 1;
}

int lzh_decompress(const u8* in, u32 n, u8* out, u32 outn)
{
  BitIn b;
  Huff lit, dist;
  u8 len[LZ_NLITLEN + LZ_NDIST];
  u32 op = 0;
  int last, have_dist;
  b.p = in;
  b.end = in + n;
  b.buf = 0;
  b.cnt = 0;
  b.over = 0;
  do {
    last = (int)bi_get(&b, 1);
    if (!read_lengths(&b, len, LZ_NLITLEN + LZ_NDIST)) return LZH_ECORRUPT;
    if (len[256] == 0 || huff_build(&lit, len, LZ_NLITLEN) != 1) return LZH_ECORRUPT;
    have_dist = huff_build(&dist, len + LZ_NLITLEN, LZ_NDIST);    // 0: no distance codes, so no matches allowed
    if (have_dist < 0) return LZH_ECORRUPT;
    for (;;) {
      int s = huff_decode(&b, &lit);
      if (b.over > 8 || s < 0) return LZH_ECORRUPT;
      if (s < 256) {
        if (op >= outn) return LZH_ECORRUPT;
        out[op++] = (u8)s;
      } else if (s == 256) {
        break;
      } else {
        u32 l, d;
        int ds;
        s -= 257;
        if (s >= LZ_NLEN || !have_dist) return LZH_ECORRUPT;
        l = g_lz_lbase[s] + bi_get(&b, g_lz_lext[s]);
        ds = huff_decode(&b, &dist);
        if (ds < 0 || ds >= LZ_NDIST) return LZH_ECORRUPT;
        d = g_lz_dbase[ds] + bi_get(&b, g_lz_dext[ds]);
        if (d > op || d > LZ_WINDOW || l > outn - op || b.over > 8) return LZH_ECORRUPT;
        {
          u8* dst = out + op;
          const u8* src = dst - d;
          u32 k;
          for (k = 0; k < l; k++) dst[k] = src[k];
        }
        op += l;
      }
    }
  } while (!last);
  if (op != outn) return LZH_ECORRUPT;
  // the bits consumed must lie inside the input (the reader may have peeked a little beyond)
  if ((u32)(b.p - in) * 8 + (u32)b.over * 8 - (u32)b.cnt > n * 8) return LZH_ECORRUPT;
  return LZH_OK;
}

// ---- filters ----

const char* filt_name(int f)
{
  static const char* const names[F_COUNT] = { "none", "delta1", "delta2", "delta3", "delta4", "wave16", "wave16s" };
  return f >= 0 && f < F_COUNT ? names[f] : "?";
}

void filt_apply(int f, const u8* in, u8* out, u32 n)
{
  u32 i;
  if (f >= F_D1 && f <= F_D4) {
    u32 s = (u32)(f - F_D1 + 1);
    for (i = 0; i < n; i++) out[i] = (u8)(i < s ? in[i] : in[i] - in[i - s]);
  } else if (f == F_W1 || f == F_W2) {
    u32 s = f == F_W1 ? 2u : 4u;      // stride in bytes
    for (i = 0; i + 1 < n; i += 2) {
      u32 cur = (u32)in[i] | ((u32)in[i + 1] << 8), r = cur;
      if (i >= s) r = (cur - ((u32)in[i - s] | ((u32)in[i - s + 1] << 8))) & 0xFFFF;
      out[i] = (u8)(r & 0xFF);
      out[i + 1] = (u8)(r >> 8);
    }
    if (n & 1) out[n - 1] = in[n - 1];
  } else {
    for (i = 0; i < n; i++) out[i] = in[i];
  }
}

void filt_undo(int f, u8* buf, u32 n)
{
  u32 i;
  if (f >= F_D1 && f <= F_D4) {
    u32 s = (u32)(f - F_D1 + 1);
    for (i = s; i < n; i++) buf[i] = (u8)(buf[i] + buf[i - s]);
  } else if (f == F_W1 || f == F_W2) {
    u32 s = f == F_W1 ? 2u : 4u;
    for (i = s; i + 1 < n; i += 2) {
      u32 r = (u32)buf[i] | ((u32)buf[i + 1] << 8);
      u32 prev = (u32)buf[i - s] | ((u32)buf[i - s + 1] << 8);
      u32 cur = (r + prev) & 0xFFFF;
      buf[i] = (u8)(cur & 0xFF);
      buf[i + 1] = (u8)(cur >> 8);
    }
  }
}
