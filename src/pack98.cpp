// pack98.cpp - Setup98 packer: manifest + files -> one setup.exe (stub + payload). Portable C++98 console tool:
// VC6 in Win98 (bin\pack98.exe) and clang on the Mac (bin/pack98) produce byte-identical output.
//
//   pack98 <manifest.ini> [-o <out.exe>] [-stub <setup98.exe>] [-q] [-v]   build the setup
//   pack98 -check <manifest.ini>                                            validate, list the files, write nothing
//   pack98 -l <setup.exe>                                                   list a setup's files
//   pack98 -t <setup.exe>                                                   test: CRCs, decompress everything
//   pack98 -x <setup.exe> <dir>                                             extract the files (and manifest.ini)
//   pack98 -m <setup.exe>                                                   print the embedded canonical manifest
// Exit codes: 0 ok, 1 error, 2 usage.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "s98types.h"
#include "crc32.h"
#include "lzh.h"
#include "manifest.h"
#include "pformat.h"
#include "wild.h"
#include "port.h"

#ifdef _WIN32
#define SEP '\\'
#else
#define SEP '/'
#endif

static int g_quiet = 0, g_verbose = 0;
static const char* g_mpath = "";

static void die(const char* fmt, ...)
{
  va_list ap;
  fprintf(stderr, "pack98: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void* xmalloc(u32 n)
{
  void* p = malloc(n ? n : 1);
  if (!p) die("out of memory (%u bytes)", n);
  return p;
}

// ---- files ----

static u8* read_file(const char* path, u32* n)
{
  FILE* f = fopen(path, "rb");
  long sz;
  u8* p;
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
  if ((unsigned long)sz > PF_MAX_FILE) { fclose(f); die("%s is larger than 256 MB", path); }
  p = (u8*)xmalloc((u32)sz + 1);
  if (fread(p, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(p); return 0; }
  fclose(f);
  p[sz] = 0;
  *n = (u32)sz;
  return p;
}

static void to_native(char* p)
{
  for (; *p; p++) if (*p == '/' || *p == '\\') *p = SEP;
}

static int is_abs(const char* p)
{
#ifdef _WIN32
  return (p[0] && p[1] == ':') || p[0] == '\\' || p[0] == '/';
#else
  return p[0] == '/';
#endif
}

// dir of a path ("." if none), native separators
static void dir_of(const char* path, char* out, int cap)
{
  const char* s = path;
  const char* last = 0;
  for (; *s; s++) if (*s == '/' || *s == '\\') last = s;
  if (!last) { s98_fmt(out, cap, "."); return; }
  s98_fmt(out, cap, "%.*s", (int)(last - path), path);
  if (!out[0]) s98_fmt(out, cap, "%c", SEP);
}

static void join(const char* a, const char* b, char* out, int cap)
{
  char bb[1024];
  s98_fmt(bb, sizeof(bb), "%s", b);
  to_native(bb);
  if (is_abs(bb) || !a[0]) s98_fmt(out, cap, "%s", bb);
  else s98_fmt(out, cap, "%s%c%s", a, SEP, bb);
}

static int is_dir(const char* p)
{
#ifdef _WIN32
  DWORD a = GetFileAttributesA(p);
  return a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void make_dirs(const char* path)
{
  char buf[1024];
  int i;
  s98_fmt(buf, sizeof(buf), "%s", path);
  for (i = 1; buf[i]; i++) {
    if (buf[i] == SEP) {
      buf[i] = 0;
      if (!is_dir(buf)) {
#ifdef _WIN32
        _mkdir(buf);
#else
        mkdir(buf, 0755);
#endif
      }
      buf[i] = SEP;
    }
  }
}

// ---- string list ----

struct StrList {
  char** v;
  int n, cap;
};

static void sl_add(StrList* l, const char* s)
{
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 64;
    l->v = (char**)realloc(l->v, (size_t)l->cap * sizeof(char*));
    if (!l->v) die("out of memory");
  }
  l->v[l->n] = (char*)xmalloc((u32)strlen(s) + 1);
  strcpy(l->v[l->n], s);
  l->n++;
}

static int skip_name(const char* name)
{
  return name[0] == '.' || path_cmp_i(name, "Thumbs.db") == 0;
}

// every file below base, as relative paths with '\' separators
static void walk(const char* base, const char* rel, StrList* out, int depth)
{
  char path[1024], sub[1024];
  if (depth > 32) die("%s: folders nested too deeply", base);
  if (rel[0]) join(base, rel, path, sizeof(path));
  else s98_fmt(path, sizeof(path), "%s", base);
#ifdef _WIN32
  {
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[1100];
    s98_fmt(pat, sizeof(pat), "%s\\*", path);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
      if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..") || skip_name(fd.cFileName)) continue;
      if (rel[0]) s98_fmt(sub, sizeof(sub), "%s\\%s", rel, fd.cFileName);
      else s98_fmt(sub, sizeof(sub), "%s", fd.cFileName);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(base, sub, out, depth + 1);
      else sl_add(out, sub);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
#else
  {
    DIR* d = opendir(path);
    struct dirent* de;
    if (!d) return;
    while ((de = readdir(d)) != 0) {
      char full[1100];
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || skip_name(de->d_name)) continue;
      if (rel[0]) s98_fmt(sub, sizeof(sub), "%s\\%s", rel, de->d_name);
      else s98_fmt(sub, sizeof(sub), "%s", de->d_name);
      s98_fmt(full, sizeof(full), "%s/%s", path, de->d_name);
      if (is_dir(full)) walk(base, sub, out, depth + 1);
      else sl_add(out, sub);
    }
    closedir(d);
  }
#endif
}

// ---- collected files ----

struct Item {
  char src[1024];          // native path to read
  char name[PF_MAX_NAME + 1];
  int keep;
  int line;
  u8* data;
  u32 usize, csize, crc, offset;
  int method, filter;
  u8* cdata;
};

static Item* g_items = 0;
static int g_nitems = 0, g_capitems = 0;

static int find_item(const char* name)
{
  int i;
  for (i = 0; i < g_nitems; i++) if (path_cmp_i(g_items[i].name, name) == 0) return i;
  return -1;
}

static int ascii_ok(const char* s)
{
  for (; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return 0;
  return 1;
}

static void add_item(const char* src, const char* name, int keep, int line)
{
  int i = find_item(name);
  if (i >= 0) {
    if (strcmp(g_items[i].src, src) != 0)
      die("%s: line %d: %s comes from two places (%s and %s, line %d)", g_mpath, line, name, g_items[i].src, src,
          g_items[i].line);
    if (keep) g_items[i].keep = 1;               // Keep wins over File for the same file
    return;
  }
  if (!ascii_ok(name)) die("%s: line %d: file name %s is not plain ASCII", g_mpath, line, name);
  if (strlen(name) > PF_MAX_NAME) die("%s: line %d: path %s is longer than %d characters", g_mpath, line, name, PF_MAX_NAME);
  if (g_nitems == g_capitems) {
    g_capitems = g_capitems ? g_capitems * 2 : 64;
    g_items = (Item*)realloc(g_items, (size_t)g_capitems * sizeof(Item));
    if (!g_items) die("out of memory");
  }
  if (g_nitems >= PF_MAX_FILES) die("more than %d files", PF_MAX_FILES);
  memset(&g_items[g_nitems], 0, sizeof(Item));
  s98_fmt(g_items[g_nitems].src, sizeof(g_items[g_nitems].src), "%s", src);
  s98_fmt(g_items[g_nitems].name, sizeof(g_items[g_nitems].name), "%s", name);
  g_items[g_nitems].keep = keep;
  g_items[g_nitems].line = line;
  g_nitems++;
}

static int item_cmp(const Item* a, const Item* b)
{
  int c = path_cmp_i(a->name, b->name);
  return c ? c : strcmp(a->name, b->name);
}

// stable merge sort (no qsort: its order of equal elements differs between C libraries)
static void sort_items(Item* v, Item* tmp, int n)
{
  int m, i, j, k;
  if (n < 2) return;
  m = n / 2;
  sort_items(v, tmp, m);
  sort_items(v + m, tmp, n - m);
  i = 0; j = m; k = 0;
  while (i < m && j < n) tmp[k++] = item_cmp(&v[j], &v[i]) < 0 ? v[j++] : v[i++];
  while (i < m) tmp[k++] = v[i++];
  while (j < n) tmp[k++] = v[j++];
  for (i = 0; i < n; i++) v[i] = tmp[i];
}

static void collect(const Manifest* m, const char* mdir)
{
  char srcdir[1024];
  StrList list;
  int r, i, j;
  int walked = 0;
  memset(&list, 0, sizeof(list));
  s98_fmt(srcdir, sizeof(srcdir), "%s", mdir);
  for (r = 0; r < m->nrules; r++) {
    const MRule* ru = &m->rules[r];
    int hits = 0;
    if (ru->kind == R_EXCLUDE) continue;
    if (ru->kind == R_SOURCE) {
      join(mdir, ru->pat, srcdir, sizeof(srcdir));
      if (!is_dir(srcdir)) die("%s: line %d: Source folder %s not found", g_mpath, ru->line, srcdir);
      for (i = 0; i < list.n; i++) free(list.v[i]);
      list.n = 0;
      walked = 0;
      continue;
    }
    if (!walked) { walk(srcdir, "", &list, 0); walked = 1; }
    for (i = 0; i < list.n; i++) {
      int excluded = 0;
      if (!wild_match(ru->pat, list.v[i])) continue;
      for (j = 0; j < m->nrules; j++)
        if (m->rules[j].kind == R_EXCLUDE && wild_match(m->rules[j].pat, list.v[i])) excluded = 1;
      if (excluded) continue;
      {
        char src[1024];
        join(srcdir, list.v[i], src, sizeof(src));
        add_item(src, ru->as[0] ? ru->as : list.v[i], ru->kind == R_KEEP, ru->line);
      }
      hits++;
    }
    if (!hits && !ru->optional)
      die("%s: line %d: %s=%s matches no file in %s (add \"| optional\" if that is fine)", g_mpath, ru->line,
          ru->kind == R_KEEP ? "Keep" : "File", ru->pat, srcdir);
    if (ru->as[0] && hits > 1) die("%s: line %d: as= with a pattern that matches %d files", g_mpath, ru->line, hits);
  }
  for (i = 0; i < list.n; i++) free(list.v[i]);
  free(list.v);
  if (!g_nitems) die("%s: no files to pack", g_mpath);
}

static void need_item(const char* what, const char* name, int line)
{
  if (find_item(name) < 0)
    die("%s: line %d: %s %s is not one of the packed files", g_mpath, line, what, name);
}

static void cross_check(Manifest* m)
{
  int i;
  char rec[64];
  need_item("MainExe", m->mainexe, m->line_mainexe);
  if (m->readme[0]) need_item("Readme", m->readme, m->line_readme);
  if (m->license[0]) need_item("License", m->license, m->line_license);
  if (find_item(m->uninstaller) >= 0) die("%s: %s is generated by Setup98; don't pack a file with that name", g_mpath, m->uninstaller);
  m_record_name(m, rec);
  if (find_item(rec) >= 0) die("%s: %s is the install record of Setup98; don't pack a file with that name", g_mpath, rec);
  for (i = 0; i < m->nlinks + m->ndlinks; i++) {
    const MLink* l = i < m->nlinks ? &m->links[i] : &m->dlinks[i - m->nlinks];
    if (path_cmp_i(l->target, m->uninstaller) != 0) need_item("shortcut target", l->target, l->line);
    if (l->icon[0]) {
      char f[M_PATH];
      char* c;
      s98_fmt(f, sizeof(f), "%s", l->icon);
      c = strrchr(f, ',');
      if (c) *c = 0;
      need_item("shortcut icon", f, l->line);
    }
  }
  if (m->has_types) {
    char f[M_PATH];
    char* c;
    s98_fmt(f, sizeof(f), "%s", m->type_icon);
    c = strrchr(f, ',');
    if (c) *c = 0;
    need_item("[Types] Icon", f, m->line_icon ? m->line_icon : m->line_mainexe);
  }
  for (i = 0; i < g_nitems; i++) {
    int k;
    for (k = 0; k < m->nuserdata; k++)
      if (wild_match(m->userdata[k], g_items[i].name))
        die("%s: %s is packed but also matches UserData=%s (user data is never shipped)", g_mpath, g_items[i].name,
            m->userdata[k]);
  }
}

// ---- compression ----

static u32 filter_score(const u8* d, u32 n)
{
  u32 i, s = 0;
  for (i = 0; i < n; i++) s += d[i] < 128 ? d[i] : 256u - d[i];
  return s;
}

static void compress_item(Item* it)
{
  u32 n = it->usize, cap = lzh_bound(n), best = 0, score[F_COUNT];
  u8 *filt, *out;
  int f, cand[3], nc = 0, k, bestf = F_NONE;
  it->method = PF_M_STORED;
  it->filter = F_NONE;
  it->csize = n;
  it->cdata = it->data;
  if (n < 32) return;
  filt = (u8*)xmalloc(n);
  out = (u8*)xmalloc(cap);
  // candidates: no filter + the two filters whose output has the smallest sum of |byte| (the PNG heuristic)
  cand[nc++] = F_NONE;
  for (f = 1; f < F_COUNT; f++) {
    filt_apply(f, it->data, filt, n);
    score[f] = filter_score(filt, n);
  }
  score[F_NONE] = filter_score(it->data, n);
  for (k = 0; k < 2; k++) {
    int bf = -1;
    for (f = 1; f < F_COUNT; f++) {
      if ((nc > 1 && cand[1] == f) || score[f] >= score[F_NONE]) continue;
      if (bf < 0 || score[f] < score[bf]) bf = f;
    }
    if (bf >= 0) cand[nc++] = bf;
  }
  for (k = 0; k < nc; k++) {
    u32 c;
    filt_apply(cand[k], it->data, filt, n);
    c = lzh_compress(filt, n, out, best ? best - 1 : (n > 16 ? n - 8 : n));
    if (c == (u32)-1) die("out of memory compressing %s", it->name);
    if (c && (!best || c < best)) {
      best = c;
      bestf = cand[k];
      if (it->cdata != it->data) free(it->cdata);
      it->cdata = (u8*)xmalloc(c);
      memcpy(it->cdata, out, c);
    }
  }
  if (best) {
    it->method = PF_M_LZH;
    it->filter = bestf;
    it->csize = best;
  }
  free(filt);
  free(out);
}

// ---- reading a built setup (for -l, -t, -x, -m) ----

struct Setup {
  u8* exe;
  u32 n;
  PfTrailer t;
  const u8* pay;
  u32 msize, nfiles, tsize;
  const char* manifest;
  const u8* toc;
  const u8* data;
};

static void open_setup(const char* path, Setup* s)
{
  s->exe = read_file(path, &s->n);
  if (!s->exe) die("cannot read %s", path);
  if (s->n < PF_TRAILER || !pf_parse_trailer(s->exe + s->n - PF_TRAILER, &s->t)) die("%s has no Setup98 payload", path);
  if (s->t.payload_offset > s->n - PF_TRAILER || s->t.payload_size != s->n - PF_TRAILER - s->t.payload_offset)
    die("%s: payload size mismatch (file truncated?)", path);
  s->pay = s->exe + s->t.payload_offset;
  if (crc32_update(0, s->pay, s->t.payload_size) != s->t.payload_crc) die("%s: payload CRC mismatch (damaged)", path);
  if (memcmp(s->pay, PF_HMAGIC, 4) != 0) die("%s: bad payload header", path);
  s->msize = get_le32(s->pay + 4);
  s->nfiles = get_le32(s->pay + 8);
  s->tsize = get_le32(s->pay + 12);
  if (PF_HEADER + s->msize + s->tsize != s->t.data_offset) die("%s: bad payload header sizes", path);
  s->manifest = (const char*)s->pay + PF_HEADER;
  s->toc = s->pay + PF_HEADER + s->msize;
  s->data = s->pay + s->t.data_offset;
}

static int setup_tool(int mode, const char* path, const char* outdir)
{
  Setup s;
  PfEntry e;
  u32 pos = 0, i, tu = 0, tc = 0, dsize;
  int bad = 0;
  open_setup(path, &s);
  dsize = s.t.payload_size - s.t.data_offset;
  if (mode == 'm') { fwrite(s.manifest, 1, s.msize, stdout); free(s.exe); return 0; }
  if (mode == 'x') {
    char mp[1024];
    FILE* f;
    s98_fmt(mp, sizeof(mp), "%s%cmanifest.ini", outdir, SEP);
    make_dirs(mp);
    f = fopen(mp, "wb");
    if (!f) die("cannot write %s", mp);
    fwrite(s.manifest, 1, s.msize, f);
    fclose(f);
  }
  for (i = 0; i < s.nfiles; i++) {
    u8* buf;
    int r = LZH_OK;
    if (!pf_read_entry(s.toc, s.tsize, &pos, &e)) die("%s: bad TOC entry %u", path, i);
    if (e.offset > dsize || e.csize > dsize - e.offset) die("%s: %s lies outside the payload", path, e.name);
    tu += e.usize;
    tc += e.csize;
    if (mode == 'l') {
      printf("%10u %10u %-7s %-4s %s\n", e.usize, e.csize, filt_name(e.filter), e.flags & PF_KEEP ? "keep" : "", e.name);
      continue;
    }
    buf = (u8*)xmalloc(e.usize);
    if (e.method == PF_M_STORED) memcpy(buf, s.data + e.offset, e.usize);
    else r = lzh_decompress(s.data + e.offset, e.csize, buf, e.usize);
    if (r == LZH_OK) filt_undo(e.filter, buf, e.usize);
    if (r != LZH_OK || crc32_update(0, buf, e.usize) != e.crc) {
      printf("BAD  %s\n", e.name);
      bad++;
    } else if (mode == 'x') {
      char op[1024], nm[PF_MAX_NAME + 1];
      FILE* f;
      s98_fmt(nm, sizeof(nm), "%s", e.name);
      to_native(nm);
      s98_fmt(op, sizeof(op), "%s%c%s", outdir, SEP, nm);
      make_dirs(op);
      f = fopen(op, "wb");
      if (!f || fwrite(buf, 1, e.usize, f) != e.usize) die("cannot write %s", op);
      fclose(f);
    }
    free(buf);
  }
  if (!g_quiet)
    printf("%s: %u files, %u -> %u bytes, %s\n", path, s.nfiles, tu, tc, bad ? "ERRORS" : "all CRCs ok");
  free(s.exe);
  return bad ? 1 : 0;
}

// ---- main ----

static void usage()
{
  fprintf(stderr,
          "Setup98 pack98 %s - build a single-file Win98 installer from a manifest\n"
          "  pack98 <manifest.ini> [-o <out.exe>] [-stub <setup98.exe>] [-q] [-v]\n"
          "  pack98 -check <manifest.ini>     validate and list the files, write nothing\n"
          "  pack98 -l|-t|-m <setup.exe>      list / test / print manifest of a built setup\n"
          "  pack98 -x <setup.exe> <dir>      extract a built setup's files\n"
          "Stub: -stub, else [Pack] Stub=, else $SETUP98_STUB, else setup98.exe next to pack98.\n",
          S98_VERSION_STR);
  exit(2);
}

static void self_dir(const char* argv0, char* out, int cap)
{
#ifdef _WIN32
  char buf[MAX_PATH];
  (void)argv0;
  GetModuleFileNameA(0, buf, sizeof(buf));
  dir_of(buf, out, cap);
#else
  dir_of(argv0, out, cap);
#endif
}

int main(int argc, char** argv)
{
  const char *mpath = 0, *outarg = 0, *stubarg = 0;
  int i, check = 0;
  char mdir[1024], outpath[1024], stubpath[1024], err[600], canon_err[600];
  static Manifest m, m2;
  static char canon[65536];
  u8 *text, *stub;
  u32 tlen, slen, clen, total_u = 0, total_c = 0, data_size = 0, toc_size = 0, pos;
  Item* tmp;
  for (i = 1; i < argc; i++) if (!strcmp(argv[i], "-q")) g_quiet = 1;
  for (i = 1; i < argc; i++) {
    const char* a = argv[i];
    if (!strcmp(a, "-l") || !strcmp(a, "-t") || !strcmp(a, "-m")) {
      if (i + 1 >= argc) usage();
      return setup_tool(a[1], argv[i + 1], 0);
    }
    if (!strcmp(a, "-x")) {
      if (i + 2 >= argc) usage();
      return setup_tool('x', argv[i + 1], argv[i + 2]);
    }
    if (!strcmp(a, "-o") && i + 1 < argc) outarg = argv[++i];
    else if (!strcmp(a, "-stub") && i + 1 < argc) stubarg = argv[++i];
    else if (!strcmp(a, "-check")) check = 1;
    else if (!strcmp(a, "-q")) g_quiet = 1;
    else if (!strcmp(a, "-v")) g_verbose = 1;
    else if (a[0] == '-') usage();
    else if (!mpath) mpath = a;
    else usage();
  }
  if (!mpath) usage();
  g_mpath = mpath;
  text = read_file(mpath, &tlen);
  if (!text) die("cannot read %s", mpath);
  if (!manifest_parse((const char*)text, tlen, &m, MP_PACK, err, sizeof(err))) die("%s: %s", mpath, err);
  dir_of(mpath, mdir, sizeof(mdir));
  collect(&m, mdir);
  tmp = (Item*)xmalloc((u32)g_nitems * (u32)sizeof(Item));
  sort_items(g_items, tmp, g_nitems);
  free(tmp);
  if (m.version_from[0]) {
    int k = find_item(m.version_from);
    u8* d;
    u32 n;
    if (k < 0) die("%s: line %d: Version=from:%s is not one of the packed files", mpath, m.line_version, m.version_from);
    d = read_file(g_items[k].src, &n);
    if (!d) die("cannot read %s", g_items[k].src);
    if (!find_fixed_version(d, n, &m.ver_ms, &m.ver_ls))
      die("%s: line %d: %s has no version resource; write the version instead (Version=1.0)", mpath, m.line_version,
          m.version_from);
    ver_format(m.ver_ms, m.ver_ls, m.version);
    free(d);
  }
  cross_check(&m);
  clen = manifest_write(&m, canon, sizeof(canon));
  if (!clen) die("canonical manifest too large");
  // the stub must be able to read back exactly what we write
  if (!manifest_parse(canon, clen, &m2, 0, canon_err, sizeof(canon_err)))
    die("internal error: canonical manifest rejected: %s", canon_err);
  if (check || g_verbose) {
    for (i = 0; i < g_nitems; i++) printf("  %s%s\n", g_items[i].name, g_items[i].keep ? "  (keep)" : "");
    printf("%s: %s %s, %d files, ok\n", mpath, m.name, m_shown_version(&m), g_nitems);
    if (check) return 0;
  }

  // stub
  if (stubarg) s98_fmt(stubpath, sizeof(stubpath), "%s", stubarg);
  else if (m.stub[0]) join(mdir, m.stub, stubpath, sizeof(stubpath));
  else if (getenv("SETUP98_STUB")) s98_fmt(stubpath, sizeof(stubpath), "%s", getenv("SETUP98_STUB"));
  else {
    char sd[1024];
    self_dir(argv[0], sd, sizeof(sd));
    join(sd, "setup98.exe", stubpath, sizeof(stubpath));
  }
  stub = read_file(stubpath, &slen);
  if (!stub) die("cannot read the stub %s (build it with nmake in M:\\dev\\setup98, or pass -stub)", stubpath);
  if (slen < 1024 || stub[0] != 'M' || stub[1] != 'Z') die("%s is not a Windows program", stubpath);
  {
    PfTrailer t;
    if (pf_parse_trailer(stub + slen - PF_TRAILER, &t)) die("%s already carries a payload: use the plain setup98.exe", stubpath);
  }

  // compress
  for (i = 0; i < g_nitems; i++) {
    Item* it = &g_items[i];
    it->data = read_file(it->src, &it->usize);
    if (!it->data) die("cannot read %s", it->src);
    it->crc = crc32_update(0, it->data, it->usize);
    compress_item(it);
    if (it->cdata != it->data) { free(it->data); it->data = 0; }
    it->offset = data_size;
    data_size += it->csize;
    total_u += it->usize;
    total_c += it->csize;
    toc_size += PF_ENTRY_FIXED + (u32)strlen(it->name);
    if (g_verbose)
      printf("%10u -> %10u %5.1f%% %-7s %s\n", it->usize, it->csize, it->usize ? 100.0 * it->csize / it->usize : 100.0,
             filt_name(it->filter), it->name);
  }

  // output
  if (outarg) s98_fmt(outpath, sizeof(outpath), "%s", outarg);
  else if (m.outfile[0]) join(mdir, m.outfile, outpath, sizeof(outpath));
  else {
    char nm[200];
    int k;
    s98_fmt(nm, sizeof(nm), "%s-%s-setup.exe", m.id, m.version);
    for (k = 0; nm[k]; k++) if (nm[k] >= 'A' && nm[k] <= 'Z') nm[k] = (char)(nm[k] + 32);
    join(mdir, nm, outpath, sizeof(outpath));
  }
  {
    u32 psize = PF_HEADER + clen + toc_size + data_size, crc;
    u8 hdr[PF_HEADER], ent[PF_ENTRY_FIXED], tr[PF_TRAILER];
    PfTrailer t;
    FILE* f;
    make_dirs(outpath);
    f = fopen(outpath, "wb");
    if (!f) die("cannot write %s", outpath);
    memcpy(hdr, PF_HMAGIC, 4);
    put_le32(hdr + 4, clen);
    put_le32(hdr + 8, (u32)g_nitems);
    put_le32(hdr + 12, toc_size);
    fwrite(stub, 1, slen, f);
    fwrite(hdr, 1, PF_HEADER, f);
    crc = crc32_update(0, hdr, PF_HEADER);
    fwrite(canon, 1, clen, f);
    crc = crc32_update(crc, (const u8*)canon, clen);
    for (i = 0; i < g_nitems; i++) {
      Item* it = &g_items[i];
      u32 nl = (u32)strlen(it->name);
      put_le32(ent, it->usize);
      put_le32(ent + 4, it->csize);
      put_le32(ent + 8, it->crc);
      put_le32(ent + 12, it->offset);
      ent[16] = (u8)it->method;
      ent[17] = (u8)it->filter;
      ent[18] = (u8)(it->keep ? PF_KEEP : 0);
      ent[19] = (u8)nl;
      fwrite(ent, 1, PF_ENTRY_FIXED, f);
      fwrite(it->name, 1, nl, f);
      crc = crc32_update(crc, ent, PF_ENTRY_FIXED);
      crc = crc32_update(crc, (const u8*)it->name, nl);
    }
    for (i = 0; i < g_nitems; i++) {
      fwrite(g_items[i].cdata, 1, g_items[i].csize, f);
      crc = crc32_update(crc, g_items[i].cdata, g_items[i].csize);
    }
    t.format = PF_FORMAT;
    t.payload_offset = slen;
    t.payload_size = psize;
    t.payload_crc = crc;
    t.data_offset = PF_HEADER + clen + toc_size;
    pf_make_trailer(tr, &t);
    fwrite(tr, 1, PF_TRAILER, f);
    pos = (u32)ftell(f);
    if (ferror(f) | fclose(f)) die("cannot write %s (disk full?)", outpath);
    if (!g_quiet)
      printf("%s: %s %s, %d files, %u -> %u bytes (%.1f%%), stub %u, setup %u bytes\n", outpath, m.name, m_shown_version(&m),
             g_nitems, total_u, total_c, total_u ? 100.0 * total_c / total_u : 100.0, slen, pos);
  }
  return 0;
}
