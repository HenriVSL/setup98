// lzhenc.cpp - LZH1 encoder (format: lzh.h). Hash chains over a 64 KB window with one-step lazy matching, then
// length-limited canonical Huffman per block. Integer-only and free of library sorts, so the output is byte-identical
// on every compiler (VC6 pack98.exe and clang pack98 must produce the same setup.exe).
#include <stdlib.h>
#include "lzh.h"
#include "lzhtab.h"

enum {
  HASH_BITS = 15,
  HASH_SIZE = 1 << HASH_BITS,
  RING = 65536,                 // prev[] ring (positions mod 65536)
  MAX_CHAIN = 192,              // candidates examined per position
  NICE_LEN = 128,               // stop searching at a match this long
  LAZY_LEN = 32,                // don't look for a better match after one this long
  TOO_FAR = 4096,               // a 3-byte match further away than this costs more than 3 literals
  BLOCK_TOKENS = 65536,
  MAXBITS = 15
};

// ---- bit writer ----

struct BitOut {
  u8* p;
  u8* end;
  u32 buf;
  int cnt;
  int full;
};

static void bo_put(BitOut* b, u32 v, int n)
{
  b->buf |= v << b->cnt;
  b->cnt += n;
  while (b->cnt >= 8) {
    if (b->p < b->end) *b->p++ = (u8)(b->buf & 0xFF);
    else b->full = 1;
    b->buf >>= 8;
    b->cnt -= 8;
  }
}

static void bo_flush(BitOut* b)
{
  if (b->cnt > 0) bo_put(b, 0, 8 - b->cnt);
}

// ---- Huffman code lengths (deterministic, limited to MAXBITS) ----

// sorts idx[0..n) by (w[idx], idx) ascending: insertion sort, stable and the same everywhere
static void sort_by_weight(int* idx, int n, const u32* w)
{
  int i, j;
  for (i = 1; i < n; i++) {
    int v = idx[i];
    j = i - 1;
    while (j >= 0 && (w[idx[j]] > w[v] || (w[idx[j]] == w[v] && idx[j] > v))) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
}

static void build_lengths(const u32* freq, int n, u8* len)
{
  u32 f[LZ_NLITLEN], wt[2 * LZ_NLITLEN];
  int leaf[LZ_NLITLEN], parent[2 * LZ_NLITLEN], depth[2 * LZ_NLITLEN];
  int i, m, maxd;
  for (i = 0; i < n; i++) { f[i] = freq[i]; len[i] = 0; }
  for (;;) {
    m = 0;
    for (i = 0; i < n; i++) if (f[i]) leaf[m++] = i;
    if (m == 0) return;
    if (m == 1) { len[leaf[0]] = 1; return; }
    sort_by_weight(leaf, m, f);
    // two-queue Huffman: nodes 0..m-1 are the sorted leaves, m..2m-2 the merged nodes in creation order
    {
      int q1 = 0, q2 = m, next = m, k;
      for (i = 0; i < m; i++) wt[i] = f[leaf[i]];
      for (k = 0; k < m - 1; k++) {
        int pick[2], t;
        for (t = 0; t < 2; t++) {
          if (q1 < m && (q2 >= next || wt[q1] <= wt[q2])) pick[t] = q1++;
          else pick[t] = q2++;
        }
        wt[next] = wt[pick[0]] + wt[pick[1]];
        parent[pick[0]] = next;
        parent[pick[1]] = next;
        next++;
      }
      depth[2 * m - 2] = 0;
      maxd = 0;
      for (i = 2 * m - 3; i >= 0; i--) {
        depth[i] = depth[parent[i]] + 1;
        if (i < m && depth[i] > maxd) maxd = depth[i];
      }
    }
    if (maxd <= MAXBITS) break;
    for (i = 0; i < n; i++) if (f[i]) f[i] = (f[i] >> 1) | 1;     // flatten and retry
  }
  for (i = 0; i < m; i++) len[leaf[i]] = (u8)depth[i];
}

// canonical codes, bit-reversed for LSB-first output
static void build_codes(const u8* len, int n, u16* code)
{
  u16 count[MAXBITS + 1], next[MAXBITS + 2];
  int i;
  u32 c = 0;
  for (i = 0; i <= MAXBITS; i++) count[i] = 0;
  for (i = 0; i < n; i++) count[len[i]]++;
  count[0] = 0;
  for (i = 1; i <= MAXBITS; i++) {
    c = (c + count[i - 1]) << 1;
    next[i] = (u16)c;
  }
  for (i = 0; i < n; i++) {
    int l = len[i], b;
    u32 x, r = 0;
    if (!l) { code[i] = 0; continue; }
    x = next[l]++;
    for (b = 0; b < l; b++) { r = (r << 1) | (x & 1); x >>= 1; }
    code[i] = (u16)r;
  }
}

static void put_lengths(BitOut* b, const u8* len, int n)
{
  int i = 0;
  while (i < n) {
    if (len[i]) { bo_put(b, len[i], 4); i++; continue; }
    {
      int run = 1;
      while (i + run < n && run < 16 && len[i + run] == 0) run++;
      bo_put(b, 0, 4);
      bo_put(b, (u32)(run - 1), 4);
      i += run;
    }
  }
}

// ---- encoder state ----

struct Enc {
  u32* head;         // HASH_SIZE: last position + 1 with this hash, 0 = none
  u32* prev;         // RING: previous position + 1 in the chain
  u32* tok;          // BLOCK_TOKENS: literal (< 256) or 0x80000000 | (len << 16) | (dist - 1)
  int ntok;
  u8 lcode[256];     // match length - 3 -> length code index
  u8 dcode_lo[512];  // distance - 1 (< 512) -> distance code
  u8 dcode_hi[256];  // (distance - 1) >> 8 -> distance code (distance > 512)
};

static void enc_tables(Enc* e)
{
  int c, l, d;
  for (c = 0; c < LZ_NLEN; c++) {
    int lo = g_lz_lbase[c], hi = c + 1 < LZ_NLEN ? g_lz_lbase[c + 1] : 259;
    if (c == LZ_NLEN - 2) hi = 258;            // 227..257 -> code 27; 258 has its own code 28
    for (l = lo; l < hi && l <= 258; l++) e->lcode[l - 3] = (u8)c;
  }
  e->lcode[258 - 3] = LZ_NLEN - 1;
  for (c = 0; c < LZ_NDIST; c++) {
    int lo = g_lz_dbase[c], hi = c + 1 < LZ_NDIST ? g_lz_dbase[c + 1] : 65537;
    for (d = lo; d < hi; d++) {
      if (d <= 512) e->dcode_lo[d - 1] = (u8)c;
      else e->dcode_hi[(d - 1) >> 8] = (u8)c;
    }
  }
}

static inline u32 dist_code(const Enc* e, u32 d)
{
  return d <= 512 ? e->dcode_lo[d - 1] : e->dcode_hi[(d - 1) >> 8];
}

static void flush_block(Enc* e, BitOut* b, int last)
{
  u32 lf[LZ_NLITLEN], df[LZ_NDIST];
  u8 len[LZ_NLITLEN + LZ_NDIST];
  u16 lc[LZ_NLITLEN], dc[LZ_NDIST];
  int i;
  for (i = 0; i < LZ_NLITLEN; i++) lf[i] = 0;
  for (i = 0; i < LZ_NDIST; i++) df[i] = 0;
  for (i = 0; i < e->ntok; i++) {
    u32 t = e->tok[i];
    if (t < 256) lf[t]++;
    else {
      u32 l = (t >> 16) & 0x1FF, d = (t & 0xFFFF) + 1;
      lf[257 + e->lcode[l - 3]]++;
      df[dist_code(e, d)]++;
    }
  }
  lf[256] = 1;
  build_lengths(lf, LZ_NLITLEN, len);
  build_lengths(df, LZ_NDIST, len + LZ_NLITLEN);
  build_codes(len, LZ_NLITLEN, lc);
  build_codes(len + LZ_NLITLEN, LZ_NDIST, dc);
  bo_put(b, last ? 1u : 0u, 1);
  put_lengths(b, len, LZ_NLITLEN + LZ_NDIST);
  for (i = 0; i < e->ntok && !b->full; i++) {
    u32 t = e->tok[i];
    if (t < 256) bo_put(b, lc[t], len[t]);
    else {
      u32 l = (t >> 16) & 0x1FF, d = (t & 0xFFFF) + 1;
      u32 c = e->lcode[l - 3], dcx = dist_code(e, d);
      bo_put(b, lc[257 + c], len[257 + c]);
      if (g_lz_lext[c]) bo_put(b, l - g_lz_lbase[c], g_lz_lext[c]);
      bo_put(b, dc[dcx], len[LZ_NLITLEN + dcx]);
      if (g_lz_dext[dcx]) bo_put(b, d - g_lz_dbase[dcx], g_lz_dext[dcx]);
    }
  }
  bo_put(b, lc[256], len[256]);
  e->ntok = 0;
}

static inline u32 hash3(const u8* p)
{
  return (((u32)p[0] << 16 | (u32)p[1] << 8 | (u32)p[2]) * 2654435761u) >> (32 - HASH_BITS);
}

static inline void insert(Enc* e, const u8* in, u32 pos)
{
  u32 h = hash3(in + pos);
  e->prev[pos & (RING - 1)] = e->head[h];
  e->head[h] = pos + 1;
}

// longest match for pos (inserted already: the chain starts at its predecessor); returns its length, *dist set
static u32 find_match(const Enc* e, const u8* in, u32 n, u32 pos, u32 prev_len, u32* dist)
{
  u32 best = prev_len, maxlen = n - pos, chain = MAX_CHAIN;
  u32 cand = e->prev[pos & (RING - 1)];
  if (maxlen > LZ_MAXMATCH) maxlen = LZ_MAXMATCH;
  *dist = 0;
  if (maxlen < LZ_MINMATCH || best >= maxlen) return 0;
  while (cand && chain--) {
    u32 c = cand - 1, l;
    const u8 *a, *b;
    if (c >= pos || pos - c > LZ_WINDOW) break;
    a = in + c;
    b = in + pos;
    if (a[best] == b[best] && a[0] == b[0] && a[1] == b[1]) {
      l = 2;
      while (l < maxlen && a[l] == b[l]) l++;
      if (l > best) {
        best = l;
        *dist = pos - c;
        if (l >= NICE_LEN || l == maxlen) break;
      }
    }
    {
      u32 nx = e->prev[c & (RING - 1)];
      if (nx >= cand) break;           // the ring slot was reused by a newer position: the chain ends here
      cand = nx;
    }
  }
  if (!*dist) return 0;
  if (best == LZ_MINMATCH && *dist > TOO_FAR) return 0;
  return best;
}

u32 lzh_compress(const u8* in, u32 n, u8* out, u32 cap)
{
  Enc e;
  BitOut b;
  u32 pos = 0, i, result;
  if (n == 0 || cap == 0) return 0;
  e.head = (u32*)malloc(HASH_SIZE * sizeof(u32));
  e.prev = (u32*)malloc(RING * sizeof(u32));
  e.tok = (u32*)malloc(BLOCK_TOKENS * sizeof(u32));
  if (!e.head || !e.prev || !e.tok) {
    free(e.head); free(e.prev); free(e.tok);
    return (u32)-1;
  }
  for (i = 0; i < HASH_SIZE; i++) e.head[i] = 0;
  for (i = 0; i < RING; i++) e.prev[i] = 0;
  e.ntok = 0;
  enc_tables(&e);
  b.p = out;
  b.end = out + cap;
  b.buf = 0;
  b.cnt = 0;
  b.full = 0;

  while (pos < n && !b.full) {
    u32 len = 0, dist = 0;
    if (n - pos >= LZ_MINMATCH) {
      insert(&e, in, pos);
      len = find_match(&e, in, n, pos, LZ_MINMATCH - 1, &dist);
      // lazy: a longer match at pos + 1 wins over this one
      while (len && len < LAZY_LEN && n - (pos + 1) >= LZ_MINMATCH) {
        u32 d2 = 0, l2;
        insert(&e, in, pos + 1);
        l2 = find_match(&e, in, n, pos + 1, len, &d2);
        if (l2 > len) {
          e.tok[e.ntok++] = in[pos];
          if (e.ntok == BLOCK_TOKENS) flush_block(&e, &b, 0);
          pos++;
          len = l2;
          dist = d2;
        } else {
          // pos + 1 is inserted already; the match covers it
          e.tok[e.ntok++] = 0x80000000u | (len << 16) | (dist - 1);
          if (e.ntok == BLOCK_TOKENS) flush_block(&e, &b, 0);
          for (i = 2; i < len; i++) if (n - (pos + i) >= LZ_MINMATCH) insert(&e, in, pos + i);
          pos += len;
          len = 0;
          dist = 0;
          goto next;
        }
      }
    }
    if (len) {
      e.tok[e.ntok++] = 0x80000000u | (len << 16) | (dist - 1);
      if (e.ntok == BLOCK_TOKENS) flush_block(&e, &b, 0);
      for (i = 1; i < len; i++) if (n - (pos + i) >= LZ_MINMATCH) insert(&e, in, pos + i);
      pos += len;
    } else {
      e.tok[e.ntok++] = in[pos];
      if (e.ntok == BLOCK_TOKENS) flush_block(&e, &b, 0);
      pos++;
    }
  next:;
  }
  flush_block(&e, &b, 1);
  bo_flush(&b);
  result = b.full ? 0 : (u32)(b.p - out);
  free(e.head);
  free(e.prev);
  free(e.tok);
  return result;
}
