// payload.cpp - Setup98 stub: reads the payload appended to its own exe (format: pformat.h), extracts files with
// CRC checks, writes the uninstaller (the stub + the manifest only) and reads/writes the install record
// (<install folder>\uninst.ini: the canonical manifest + [Installed*] sections with files, folders and shortcuts).
#include <windows.h>
#include <string.h>
#include "s98.h"
#include "lzh.h"
#include "crc32.h"

Payload g_pay;

static void* halloc(u32 n)
{
  return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}

static void hfree(void* p)
{
  if (p) HeapFree(GetProcessHeap(), 0, p);
}

static int read_at(HANDLE f, u32 pos, void* buf, u32 n)
{
  DWORD got = 0;
  if (SetFilePointer(f, (LONG)pos, 0, FILE_BEGIN) == 0xFFFFFFFF && GetLastError() != NO_ERROR) return 0;
  return ReadFile(f, buf, n, &got, 0) && got == n;
}

int pay_open(char* err, int errcap)
{
  char self[MAX_PATH];
  u8 tr[PF_TRAILER], hdr[PF_HEADER];
  u8* meta = 0;
  u8* chunk = 0;
  DWORD size;
  u32 pos, i, crc = 0, tpos;
  memset(&g_pay, 0, sizeof(g_pay));
  err[0] = 0;
  GetModuleFileNameA(0, self, sizeof(self));
  g_pay.f = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (g_pay.f == INVALID_HANDLE_VALUE) { s_copy(err, errcap, "Setup cannot read its own file."); return 0; }
  size = GetFileSize(g_pay.f, 0);
  if (size == 0xFFFFFFFF || size < PF_TRAILER || !read_at(g_pay.f, size - PF_TRAILER, tr, PF_TRAILER)) {
    s_copy(err, errcap, "Setup cannot read its own file.");
    return 0;
  }
  if (!pf_parse_trailer(tr, &g_pay.t)) return 0;          // no payload: the plain stub
  g_pay.present = 1;
  s_copy(err, errcap, "This setup file is damaged (it may be incomplete). Please download or copy it again.");
  if (g_pay.t.payload_offset > size - PF_TRAILER || g_pay.t.payload_size != size - PF_TRAILER - g_pay.t.payload_offset)
    return 0;
  // whole-payload CRC first: a truncated or damaged download is refused before anything happens
  chunk = (u8*)halloc(65536);
  if (!chunk) return 0;
  for (pos = 0; pos < g_pay.t.payload_size;) {
    u32 n = g_pay.t.payload_size - pos;
    if (n > 65536) n = 65536;
    if (!read_at(g_pay.f, g_pay.t.payload_offset + pos, chunk, n)) { hfree(chunk); return 0; }
    crc = crc32_update(crc, chunk, n);
    pos += n;
  }
  hfree(chunk);
  if (crc != g_pay.t.payload_crc) return 0;
  if (!read_at(g_pay.f, g_pay.t.payload_offset, hdr, PF_HEADER) || memcmp(hdr, PF_HMAGIC, 4) != 0) return 0;
  g_pay.msize = get_le32(hdr + 4);
  g_pay.nfiles = get_le32(hdr + 8);
  tpos = get_le32(hdr + 12);       // toc size
  if (g_pay.nfiles > PF_MAX_FILES || g_pay.msize > 65536 || PF_HEADER + g_pay.msize + tpos != g_pay.t.data_offset)
    return 0;
  meta = (u8*)halloc(g_pay.msize + tpos + 1);
  g_pay.files = (PfEntry*)halloc((g_pay.nfiles ? g_pay.nfiles : 1) * (u32)sizeof(PfEntry));
  if (!meta || !g_pay.files) return 0;
  if (!read_at(g_pay.f, g_pay.t.payload_offset + PF_HEADER, meta, g_pay.msize + tpos)) return 0;
  g_pay.manifest = (char*)halloc(g_pay.msize + 1);
  if (!g_pay.manifest) return 0;
  memcpy(g_pay.manifest, meta, g_pay.msize);
  g_pay.manifest[g_pay.msize] = 0;
  pos = 0;
  for (i = 0; i < g_pay.nfiles; i++) {
    PfEntry* e = &g_pay.files[i];
    u32 dsize = g_pay.t.payload_size - g_pay.t.data_offset;
    if (!pf_read_entry(meta + g_pay.msize, tpos, &pos, e)) { hfree(meta); return 0; }
    if (e->offset > dsize || e->csize > dsize - e->offset) { hfree(meta); return 0; }
    g_pay.total_usize += e->usize;
    if (e->usize > g_pay.max_usize) g_pay.max_usize = e->usize;
    if (e->csize > g_pay.max_csize) g_pay.max_csize = e->csize;
  }
  hfree(meta);
  {
    char perr[300];
    if (!manifest_parse(g_pay.manifest, g_pay.msize, &g_m, 0, perr, sizeof(perr))) {
      slog("error: embedded manifest: %s", perr);
      return 0;
    }
  }
  err[0] = 0;
  return 1;
}

int pay_find(const char* name)
{
  u32 i;
  for (i = 0; i < g_pay.nfiles; i++) if (lstrcmpiA(g_pay.files[i].name, name) == 0) return (int)i;
  return -1;
}

int pay_extract(int i, u8* buf, u8* cbuf)
{
  const PfEntry* e = &g_pay.files[i];
  u32 pos = g_pay.t.payload_offset + g_pay.t.data_offset + e->offset;
  if (e->method == PF_M_STORED) {
    if (e->usize && !read_at(g_pay.f, pos, buf, e->usize)) return 0;
  } else {
    if (!read_at(g_pay.f, pos, cbuf, e->csize)) return 0;
    if (lzh_decompress(cbuf, e->csize, buf, e->usize) != LZH_OK) return -1;
    filt_undo(e->filter, buf, e->usize);
  }
  return crc32_update(0, buf, e->usize) == e->crc ? 1 : -1;
}

u8* pay_load_text(const char* name, u32* n)
{
  int i = pay_find(name);
  u8 *raw, *cbuf, *out;
  u32 k, o = 0, len;
  if (i < 0) return 0;
  len = g_pay.files[i].usize;
  raw = (u8*)halloc(len + 1);
  cbuf = (u8*)halloc(g_pay.files[i].csize + 1);
  out = (u8*)halloc(len * 2 + 1);
  if (!raw || !cbuf || !out || pay_extract(i, raw, cbuf) != 1) { hfree(raw); hfree(cbuf); hfree(out); return 0; }
  for (k = 0; k < len; k++) {           // LF -> CRLF for the edit control; NULs become spaces
    if (raw[k] == '\n' && (k == 0 || raw[k - 1] != '\r')) out[o++] = '\r';
    out[o++] = raw[k] ? raw[k] : ' ';
  }
  out[o] = 0;
  *n = o;
  hfree(raw);
  hfree(cbuf);
  return out;
}

int pay_write_uninstaller(const char* path, u32* crc_out)
{
  HANDLE h;
  u8* buf;
  u8 hdr[PF_HEADER], tr[PF_TRAILER];
  u32 pos, crc = 0, pcrc;
  DWORD n;
  PfTrailer t;
  int ok = 1;
  buf = (u8*)halloc(65536);
  if (!buf) return 0;
  SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
  h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE) { hfree(buf); return 0; }
  for (pos = 0; ok && pos < g_pay.t.payload_offset;) {        // the stub
    u32 c = g_pay.t.payload_offset - pos;
    if (c > 65536) c = 65536;
    ok = read_at(g_pay.f, pos, buf, c) && WriteFile(h, buf, c, &n, 0) && n == c;
    crc = crc32_update(crc, buf, c);
    pos += c;
  }
  memcpy(hdr, PF_HMAGIC, 4);
  put_le32(hdr + 4, g_pay.msize);
  put_le32(hdr + 8, 0);
  put_le32(hdr + 12, 0);
  pcrc = crc32_update(0, hdr, PF_HEADER);
  pcrc = crc32_update(pcrc, (const u8*)g_pay.manifest, g_pay.msize);
  t.format = PF_FORMAT;
  t.payload_offset = g_pay.t.payload_offset;
  t.payload_size = PF_HEADER + g_pay.msize;
  t.payload_crc = pcrc;
  t.data_offset = PF_HEADER + g_pay.msize;
  pf_make_trailer(tr, &t);
  if (ok) ok = WriteFile(h, hdr, PF_HEADER, &n, 0) && n == PF_HEADER;
  if (ok) ok = WriteFile(h, g_pay.manifest, g_pay.msize, &n, 0) && n == g_pay.msize;
  if (ok) ok = WriteFile(h, tr, PF_TRAILER, &n, 0) && n == PF_TRAILER;
  crc = crc32_update(crc, hdr, PF_HEADER);
  crc = crc32_update(crc, (const u8*)g_pay.manifest, g_pay.msize);
  crc = crc32_update(crc, tr, PF_TRAILER);
  if (!CloseHandle(h)) ok = 0;
  if (!ok) DeleteFileA(path);
  hfree(buf);
  if (crc_out) *crc_out = crc;
  return ok;
}

// ---- install record ----

void rec_init(Record* r)
{
  memset(r, 0, sizeof(*r));
}

void rec_free(Record* r)
{
  hfree(r->files);
  hfree(r->dirs);
  hfree(r->m);
  rec_init(r);
}

int rec_find_file(const Record* r, const char* name)
{
  int i;
  for (i = 0; i < r->nfiles; i++) if (lstrcmpiA(r->files[i].name, name) == 0) return i;
  return -1;
}

int rec_add_file(Record* r, const char* name, char kind, int has_crc, u32 crc)
{
  int i = rec_find_file(r, name);
  if (i < 0) {
    if (r->nfiles == r->capfiles) {
      int cap = r->capfiles ? r->capfiles * 2 : 64;
      RecFile* nf = (RecFile*)halloc((u32)cap * (u32)sizeof(RecFile));
      if (!nf) return 0;
      if (r->nfiles) memcpy(nf, r->files, (u32)r->nfiles * sizeof(RecFile));
      hfree(r->files);
      r->files = nf;
      r->capfiles = cap;
    }
    i = r->nfiles++;
  }
  s_copy(r->files[i].name, sizeof(r->files[i].name), name);
  r->files[i].kind = kind;
  r->files[i].has_crc = has_crc;
  r->files[i].crc = crc;
  return 1;
}

int rec_find_dir(const Record* r, const char* rel)
{
  int i;
  for (i = 0; i < r->ndirs; i++) if (lstrcmpiA(r->dirs[i], rel) == 0) return i;
  return -1;
}

int rec_add_dir(Record* r, const char* rel)
{
  if (rec_find_dir(r, rel) >= 0) return 1;
  if (r->ndirs == r->capdirs) {
    int cap = r->capdirs ? r->capdirs * 2 : 32;
    char (*nd)[PF_MAX_NAME + 1] = (char (*)[PF_MAX_NAME + 1])halloc((u32)cap * (PF_MAX_NAME + 1));
    if (!nd) return 0;
    if (r->ndirs) memcpy(nd, r->dirs, (u32)r->ndirs * (PF_MAX_NAME + 1));
    hfree(r->dirs);
    r->dirs = nd;
    r->capdirs = cap;
  }
  s_copy(r->dirs[r->ndirs++], PF_MAX_NAME + 1, rel);
  return 1;
}

static u32 hex32(const char* s, int* ok)
{
  u32 v = 0;
  int i;
  *ok = 0;
  for (i = 0; i < 8; i++) {
    char c = s[i];
    if (c >= '0' && c <= '9') v = (v << 4) | (u32)(c - '0');
    else if (c >= 'A' && c <= 'F') v = (v << 4) | (u32)(c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') v = (v << 4) | (u32)(c - 'a' + 10);
    else return 0;
  }
  *ok = 1;
  return v;
}

int rec_load(Record* r, const char* dir)
{
  char path[MAX_PATH], name[64], key[300], files[8200];
  HANDLE h;
  DWORD size, got = 0;
  char* text;
  rec_init(r);
  m_record_name(&g_m, name);
  p_join(dir, name, path, sizeof(path));
  h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (h != INVALID_HANDLE_VALUE) {
    size = GetFileSize(h, 0);
    text = size < 4000000 ? (char*)halloc(size + 1) : 0;
    if (text && ReadFile(h, text, size, &got, 0) && got == size) {
      char* p = text;
      int sec = 0;
      char err[300];
      text[size] = 0;
      r->loaded = 1;
      r->m = (Manifest*)halloc(sizeof(Manifest));
      if (r->m && manifest_parse(text, size, r->m, MP_RECORD, err, sizeof(err))) r->has_manifest = 1;
      else slog("record %s: manifest not readable (%s)", path, r->m ? err : "memory");
      while (*p) {
        char* e = p;
        char line[MAX_PATH + 40];
        int len;
        while (*e && *e != '\n') e++;
        len = (int)(e - p);
        if (len > 0 && p[len - 1] == '\r') len--;
        lstrcpynA(line, p, len + 1 < (int)sizeof(line) ? len + 1 : (int)sizeof(line));
        p = *e ? e + 1 : e;
        if (line[0] == '[') {
          sec = !lstrcmpiA(line, "[InstalledFiles]") ? 1 : !lstrcmpiA(line, "[InstalledDirs]") ? 2 :
                !lstrcmpiA(line, "[InstalledLinks]") ? 3 : 0;
          continue;
        }
        if (!line[0] || line[0] == ';') continue;
        if (sec == 1 && lstrlenA(line) > 11 && line[1] == ' ' && line[10] == ' ' &&
            (line[0] == RF_PROGRAM || line[0] == RF_KEEP)) {
          int ok = 0;
          u32 crc = line[2] == '-' ? 0 : hex32(line + 2, &ok);
          rec_add_file(r, line + 11, line[0], ok, crc);
        } else if (sec == 2) {
          rec_add_dir(r, line);
        } else if (sec == 3 && r->nlinks < (int)(sizeof(r->links) / sizeof(r->links[0]))) {
          s_copy(r->links[r->nlinks++], MAX_PATH, line);
        }
      }
    }
    hfree(text);
    CloseHandle(h);
    if (r->loaded) { slog("record %s: %d files, %d folders, %d shortcuts", path, r->nfiles, r->ndirs, r->nlinks); return 1; }
  }
  // no record: an install made by an older setup (Pica 1.0) lists its program files in the registry
  key_app(key);
  if (reg_get(HKEY_LOCAL_MACHINE, key, "Files", files, sizeof(files)) == 1) {
    char* s = files;
    while (*s) {
      char* e = s;
      char nm[MAX_PATH];
      while (*e && *e != ';') e++;
      lstrcpynA(nm, s, (int)(e - s) + 1 < MAX_PATH ? (int)(e - s) + 1 : MAX_PATH);
      if (nm[0]) rec_add_file(r, nm, RF_PROGRAM, 0, 0);
      s = *e ? e + 1 : e;
    }
    slog("no install record; registry Files lists %d files", r->nfiles);
    return 1;
  }
  return 0;
}

static int wr(HANDLE h, const char* s)
{
  DWORD n;
  int len = lstrlenA(s);
  return WriteFile(h, s, (DWORD)len, &n, 0) && n == (DWORD)len;
}

int rec_save(const Record* r, const Manifest* m, const Opts* o, const char* renames)
{
  char path[MAX_PATH], name[64], line[MAX_PATH + 64];
  static char canon[65536];
  HANDLE h;
  u32 clen;
  int i, ok;
  m_record_name(m, name);
  p_join(o->dir, name, path, sizeof(path));
  clen = manifest_write(m, canon, sizeof(canon));
  SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
  h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE) { slog("error: cannot write %s (%lu)", path, GetLastError()); return 0; }
  ok = wr(h, "; Setup98 install record: what Setup installed here. The uninstaller needs it.\r\n") && wr(h, canon);
  wsprintfA(line, "[Installed]\r\nSetup98=%s\r\nVersion=%s\r\n", S98_VERSION_STR, m->version);
  ok = ok && wr(h, line);
  if (m->display_version[0]) {
    wsprintfA(line, "DisplayVersion=%s\r\n", m->display_version);
    ok = ok && wr(h, line);
  }
  s_copy(line, sizeof(line), "Dir=");
  s_cat(line, sizeof(line), o->dir);
  s_cat(line, sizeof(line), "\r\n[InstalledFiles]\r\n");
  ok = ok && wr(h, line);
  for (i = 0; ok && i < r->nfiles; i++) {
    if (r->files[i].has_crc) wsprintfA(line, "%c %08lX ", r->files[i].kind, r->files[i].crc);
    else wsprintfA(line, "%c -------- ", r->files[i].kind);
    s_cat(line, sizeof(line), r->files[i].name);
    s_cat(line, sizeof(line), "\r\n");
    ok = wr(h, line);
  }
  ok = ok && wr(h, "[InstalledDirs]\r\n");
  for (i = 0; ok && i < r->ndirs; i++) ok = wr(h, r->dirs[i]) && wr(h, "\r\n");
  ok = ok && wr(h, "[InstalledLinks]\r\n");
  for (i = 0; ok && i < r->nlinks; i++) ok = wr(h, r->links[i]) && wr(h, "\r\n");
  if (ok && renames && renames[0]) ok = wr(h, "[InstalledRenames]\r\n") && wr(h, renames);
  if (!CloseHandle(h)) ok = 0;
  if (!ok) slog("error: cannot write %s", path);
  else slog("record %s: %d files, %d folders, %d shortcuts", path, r->nfiles, r->ndirs, r->nlinks);
  return ok;
}

// RunOnce after a restart: WININIT.INI renamed replaced files to their 8.3 names; give them their long names back
int fix_names(const char* dir)
{
  char path[MAX_PATH], name[64];
  HANDLE h;
  DWORD size, got = 0;
  char* text;
  int sec = 0, fixed = 0;
  m_record_name(&g_m, name);
  p_join(dir, name, path, sizeof(path));
  h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (h == INVALID_HANDLE_VALUE) { slog("fix names: no record %s", path); return 0; }
  size = GetFileSize(h, 0);
  text = size < 4000000 ? (char*)halloc(size + 1) : 0;
  if (text && ReadFile(h, text, size, &got, 0) && got == size) {
    char* p = text;
    text[size] = 0;
    while (*p) {
      char* e = p;
      char line[MAX_PATH * 2 + 8];
      char* bar;
      int len;
      while (*e && *e != '\n') e++;
      len = (int)(e - p);
      if (len > 0 && p[len - 1] == '\r') len--;
      lstrcpynA(line, p, len + 1 < (int)sizeof(line) ? len + 1 : (int)sizeof(line));
      p = *e ? e + 1 : e;
      if (line[0] == '[') { sec = !lstrcmpiA(line, "[InstalledRenames]"); continue; }
      bar = strchr(line, '|');
      if (!sec || !bar) continue;
      *bar = 0;
      if (p_exists(line) && !p_exists(bar + 1)) {
        if (MoveFileA(line, bar + 1)) { slog("renamed %s -> %s", line, bar + 1); fixed++; }
        else slog("error: cannot rename %s -> %s (%lu)", line, bar + 1, GetLastError());
      }
    }
  }
  if (text) HeapFree(GetProcessHeap(), 0, text);
  CloseHandle(h);
  return fixed;
}
