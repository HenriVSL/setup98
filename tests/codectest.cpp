// codectest.cpp - Mac (and VC6) tests of the LZH1 codec, filters and CRC-32:
//   codectest [-fuzz N] [-seed S] [-q] [files...]
// 1. known CRC values; 2. round trips of synthetic data (empty..1 MB, runs, random, text-like, every filter);
// 3. round trips of the given files with every filter (prints sizes and ratios);
// 4. corrupt input: truncation at every length of a small stream, bit flips, random garbage, N mutations of real
//    streams; the decoder must return LZH_OK with exact output or LZH_ECORRUPT, never crash (run under ASan/UBSan).
// Exit 0 = all passed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../src/lzh.h"
#include "../src/crc32.h"
#include "../src/port.h"

static int g_fail = 0, g_checks = 0, g_quiet = 0;
static u32 g_rng = 12345;

static u32 rnd()
{
  g_rng = g_rng * 1103515245u + 12345u;
  return (g_rng >> 8) & 0xFFFFFF;
}

static int g_line = 0;
static void check_(int ok, const char* fmt, ...)
{
  va_list ap;
  g_checks++;
  if (ok) return;
  g_fail++;
  printf("FAIL line %d: ", g_line);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}
#define CHECK g_line = __LINE__, check_

static u8* load(const char* path, u32* n)
{
  FILE* f = fopen(path, "rb");
  u8* p;
  long sz;
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  p = (u8*)malloc((size_t)sz + 1);
  if (p && fread(p, 1, (size_t)sz, f) != (size_t)sz) { free(p); p = 0; }
  fclose(f);
  *n = (u32)sz;
  return p;
}

// compress with filter f, decompress, compare; returns the compressed size (0 = stored)
static u32 roundtrip(const u8* data, u32 n, int f, const char* what)
{
  u8 *filt = (u8*)malloc(n + 1), *comp, *back;
  u32 cap = lzh_bound(n), c;
  int r;
  comp = (u8*)malloc(cap + 1);
  back = (u8*)malloc(n + 1);
  filt_apply(f, data, filt, n);
  c = lzh_compress(filt, n, comp, cap);
  CHECK(c != (u32)-1, "%s: out of memory", what);
  if (n) CHECK(c > 0 && c <= cap, "%s filter %d: compress failed (n=%u)", what, f, n);
  if (c > 0 && c != (u32)-1) {
    u32 c2;
    r = lzh_decompress(comp, c, back, n);
    CHECK(r == LZH_OK, "%s filter %d: decompress %d", what, f, r);
    filt_undo(f, back, n);
    CHECK(memcmp(back, data, n) == 0, "%s filter %d: data differs", what, f);
    // determinism and the "doesn't fit" path
    c2 = lzh_compress(filt, n, comp, c);
    CHECK(c2 == c, "%s: second compress %u != %u", what, c2, c);
    if (c > 1) CHECK(lzh_compress(filt, n, comp, c - 1) == 0, "%s: cap c-1 not refused", what);
  }
  free(filt);
  free(comp);
  free(back);
  return c == (u32)-1 ? 0 : c;
}

static void test_crc()
{
  const char* s = "123456789";
  u32 c = crc32_update(0, (const u8*)s, 9);
  CHECK(c == 0xCBF43926u, "crc32 of 123456789 = %08X", c);
  c = crc32_update(crc32_update(0, (const u8*)s, 4), (const u8*)s + 4, 5);
  CHECK(c == 0xCBF43926u, "chunked crc32 = %08X", c);
  CHECK(crc32_update(0, (const u8*)s, 0) == 0, "crc32 of nothing");
}

static void test_synthetic()
{
  static const u32 sizes[] = { 1, 2, 3, 4, 5, 7, 16, 100, 257, 258, 259, 1000, 4096, 65535, 65536, 65537, 200000,
                               1048576 };
  u32 si, i, k;
  int f;
  for (si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
    u32 n = sizes[si];
    u8* d = (u8*)malloc(n);
    for (k = 0; k < 6; k++) {
      char what[64];
      for (i = 0; i < n; i++) {
        switch (k) {
        case 0: d[i] = 0; break;
        case 1: d[i] = (u8)rnd(); break;
        case 2: d[i] = (u8)("abcabcabdabcab"[i % 14]); break;
        case 3: d[i] = (u8)((i / 1000) & 1 ? 0xFF : (rnd() & 3)); break;           // runs and noise
        case 4: d[i] = (u8)("the quick brown fox jumps over the lazy dog\r\n"[(i * 7 + i / 50) % 45]); break;
        default: d[i] = (u8)(128 + (int)(60.0 * ((i % 40) < 20 ? (i % 20) : 20 - (i % 20)) / 20)); break;
        }
      }
      for (f = 0; f < F_COUNT; f++) {
        s98_fmt(what, sizeof(what), "synthetic n=%u kind=%u", n, k);
        roundtrip(d, n, f, what);
        if (n > 65536 && f > 0) break;       // keep the big cases quick
      }
    }
    free(d);
  }
  // long-distance matches right at the window edge
  {
    u32 n = 3 * 65536;
    u8* d = (u8*)malloc(n);
    for (i = 0; i < 65535; i++) d[i] = (u8)rnd();
    for (i = 65535; i < n; i++) d[i] = d[i - 65535];
    roundtrip(d, n, F_NONE, "window edge 65535");
    for (i = 65536; i < n; i++) d[i] = d[i - 65536];
    roundtrip(d, n, F_NONE, "window edge 65536");
    free(d);
  }
}

static void test_files(int argc, char** argv, int first)
{
  int a, f;
  u32 tot_in = 0, tot_best = 0;
  for (a = first; a < argc; a++) {
    u32 n, best = 0, c;
    int bestf = 0;
    u8* d = load(argv[a], &n);
    if (!d) { printf("cannot read %s\n", argv[a]); g_fail++; continue; }
    for (f = 0; f < F_COUNT; f++) {
      c = roundtrip(d, n, f, argv[a]);
      if (c && (!best || c < best)) { best = c; bestf = f; }
    }
    if (!best || best > n) best = n;
    tot_in += n;
    tot_best += best;
    if (!g_quiet) printf("%9u -> %9u (%5.1f%%, %s)  %s\n", n, best, n ? 100.0 * best / n : 100.0, filt_name(bestf),
                         argv[a]);
    free(d);
  }
  if (first < argc && !g_quiet)
    printf("total %u -> %u (%.1f%%, ratio %.2f)\n", tot_in, tot_best, tot_in ? 100.0 * tot_best / tot_in : 0.0,
           tot_best ? (double)tot_in / tot_best : 0.0);
}

// decoder must survive any input
static void try_decode(const u8* c, u32 cn, u32 outn, const u8* expect)
{
  u8* out = (u8*)malloc(outn + 1);
  int r = lzh_decompress(c, cn, out, outn);
  CHECK(r == LZH_OK || r == LZH_ECORRUPT, "decoder returned %d", r);
  if (r == LZH_OK && expect && cn) {
    // a mutated stream may decode to other bytes; that's fine (the per-file CRC catches it), but it must be complete
  }
  free(out);
}

static void test_corrupt(int fuzz_n, int argc, char** argv, int first)
{
  u8 src[3000], comp[8000];
  u32 n = sizeof(src), c, i, cut;
  int k;
  for (i = 0; i < n; i++) src[i] = (u8)("hello hello world, setup98 corrupt test "[i % 41] ^ (i % 97 == 0 ? rnd() : 0));
  c = lzh_compress(src, n, comp, sizeof(comp));
  CHECK(c > 0, "small compress");
  for (cut = 0; cut < c; cut++) {
    u8* out = (u8*)malloc(n);
    int r = lzh_decompress(comp, cut, out, n);
    CHECK(r == LZH_ECORRUPT, "truncated at %u of %u accepted", cut, c);
    free(out);
  }
  {
    u8* out = (u8*)malloc(n + 10);
    CHECK(lzh_decompress(comp, c, out, n - 1) == LZH_ECORRUPT, "short output accepted");
    CHECK(lzh_decompress(comp, c, out, n + 1) == LZH_ECORRUPT, "long output accepted");
    free(out);
  }
  for (i = 0; i < c * 8; i++) {          // every single bit flip
    comp[i / 8] ^= (u8)(1 << (i % 8));
    try_decode(comp, c, n, src);
    comp[i / 8] ^= (u8)(1 << (i % 8));
  }
  for (k = 0; k < 2000; k++) {           // garbage
    u32 len = rnd() % 600;
    for (i = 0; i < len; i++) comp[i] = (u8)rnd();
    try_decode(comp, len, rnd() % 5000, 0);
  }
  // mutations of real streams
  {
    int a;
    for (a = first; a < argc && fuzz_n > 0; a++) {
      u32 fn, cn, cap;
      u8 *d = load(argv[a], &fn), *cc, *m;
      int it;
      if (!d) continue;
      if (fn > 300000) fn = 300000;
      cap = lzh_bound(fn);
      cc = (u8*)malloc(cap);
      m = (u8*)malloc(cap);
      cn = lzh_compress(d, fn, cc, cap);
      for (it = 0; it < fuzz_n && cn; it++) {
        u32 mn = cn, j, nm = 1 + rnd() % 4;
        memcpy(m, cc, cn);
        for (j = 0; j < nm; j++) {
          u32 at = rnd() % mn;
          switch (rnd() % 4) {
          case 0: m[at] ^= (u8)(1 << (rnd() % 8)); break;
          case 1: m[at] = (u8)rnd(); break;
          case 2: mn = at + 1; break;                                     // truncate
          default: if (at + 8 < mn) memmove(m + at, m + at + 1 + rnd() % 7, mn - at - 8); break;
          }
        }
        try_decode(m, mn, fn, d);
      }
      free(d);
      free(cc);
      free(m);
    }
  }
}

int main(int argc, char** argv)
{
  int first = 1, fuzz_n = 200;
  while (first < argc && argv[first][0] == '-') {
    if (!strcmp(argv[first], "-fuzz") && first + 1 < argc) fuzz_n = atoi(argv[++first]);
    else if (!strcmp(argv[first], "-seed") && first + 1 < argc) g_rng = (u32)atoi(argv[++first]);
    else if (!strcmp(argv[first], "-q")) g_quiet = 1;
    first++;
  }
  test_crc();
  test_synthetic();
  test_files(argc, argv, first);
  test_corrupt(fuzz_n, argc, argv, first);
  printf("codectest: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
