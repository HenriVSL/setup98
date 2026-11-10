// inst.cpp - Setup98 install / upgrade / uninstall work, generalised from Pica's setup. No UI here (setup.cpp asks
// the questions). Every step is logged. Upgrade rules (README.md "Upgrade rules"):
//   program files  replaced every time (written next to the target as ~S98nnnn.TMP first, then swapped in; a file in
//                  use is swapped by WININIT.INI at the next start)
//   Keep files     installed only if missing, never overwritten; a plain uninstall deletes them only if unchanged
//                  (same CRC as installed), /PURGE always
//   UserData       never shipped; deleted only by a purge
//   Generated      deleted by every uninstall
//   obsolete       program files the old version installed and this one doesn't ship are deleted
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "s98.h"
#include "wild.h"
#include "crc32.h"

static void* halloc(u32 n)
{
  return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}

static void hfree(void* p)
{
  if (p) HeapFree(GetProcessHeap(), 0, p);
}

// ---- versions, detection, defaults ----

int file_version(const char* path, DWORD* ms, DWORD* ls)
{
  DWORD dummy = 0, size;
  char* buf;
  VS_FIXEDFILEINFO* f = 0;
  UINT flen = 0;
  int ok = 0;
  size = GetFileVersionInfoSizeA((char*)path, &dummy);
  if (!size) return 0;
  buf = (char*)halloc(size);
  if (!buf) return 0;
  if (GetFileVersionInfoA((char*)path, 0, size, buf) && VerQueryValueA(buf, (char*)"\\", (void**)&f, &flen) && f &&
      flen >= sizeof(VS_FIXEDFILEINFO)) {
    *ms = f->dwFileVersionMS;
    *ls = f->dwFileVersionLS;
    ok = 1;
  }
  hfree(buf);
  return ok;
}

void detect_install(InstallInfo* ii)
{
  char exe[MAX_PATH], v[64], key[300];
  u32 ms, ls;
  memset(ii, 0, sizeof(*ii));
  key_app(key);
  if (reg_get(HKEY_LOCAL_MACHINE, key, "InstallDir", ii->dir, sizeof(ii->dir)) != 1 || !ii->dir[0]) return;
  ii->installed = 1;
  // the version Setup recorded wins (a program without a version resource still has one); else the exe's
  if (reg_get(HKEY_LOCAL_MACHINE, key, "Version", v, sizeof(v)) == 1 && ver_parse(v, &ms, &ls)) {
    ii->ms = ms;
    ii->ls = ls;
    ii->have_ver = 1;
  } else {
    p_join(ii->dir, g_m.mainexe, exe, sizeof(exe));
    if (file_version(exe, &ii->ms, &ii->ls)) ii->have_ver = 1;
  }
  if (ii->have_ver) ver_format(ii->ms, ii->ls, ii->ver);
  else s_copy(ii->ver, sizeof(ii->ver), "?");
}

void opts_default_types(Opts* o)
{
  int i;
  for (i = 0; i < g_m.next; i++) o->types[i] = g_m.exts[i].def;
}

void opts_defaults(Opts* o, const InstallInfo* ii)
{
  char types[1200], key[300];
  int i;
  opts_default_types(o);
  o->startmenu = g_m.startmenu == OPT_YES;
  o->desktop = g_m.desktop == OPT_YES;
  o->context = g_m.has_context && g_m.ctx_default;
  key_app(key);
  if (ii && ii->installed) {
    s_copy(o->dir, sizeof(o->dir), ii->dir);
    o->startmenu = (int)reg_get_dw(HKEY_LOCAL_MACHINE, key, "StartMenu", (DWORD)o->startmenu);
    o->desktop = (int)reg_get_dw(HKEY_LOCAL_MACHINE, key, "Desktop", (DWORD)o->desktop);
    o->context = (int)reg_get_dw(HKEY_LOCAL_MACHINE, key, "ContextMenu", (DWORD)o->context);
    if (reg_get(HKEY_LOCAL_MACHINE, key, "Types", types + 1, sizeof(types) - 2) == 1) {
      types[0] = ';';
      s_cat(types, sizeof(types), ";");
      CharLowerA(types);
      for (i = 0; i < g_m.next; i++) {
        char pat[24];
        s_copy(pat, sizeof(pat), ";");
        s_cat(pat, sizeof(pat), g_m.exts[i].ext);
        s_cat(pat, sizeof(pat), ";");
        o->types[i] = strstr(types, pat) != 0;
      }
    }
  } else {
    expand_folder(g_m.folder, o->dir, sizeof(o->dir));
  }
  if (g_m.startmenu == OPT_OFF || !g_m.nlinks) o->startmenu = 0;
  if (g_m.desktop == OPT_OFF || !g_m.ndlinks) o->desktop = 0;
  if (!g_m.has_context) o->context = 0;
}

int app_running(const char* dir)
{
  char exe[MAX_PATH];
  if ((g_m.window_class[0] || g_m.window_title[0]) &&
      FindWindowA(g_m.window_class[0] ? g_m.window_class : 0, g_m.window_title[0] ? g_m.window_title : 0))
    return 1;
  if (dir && dir[0]) {
    HANDLE h;
    p_join(dir, g_m.mainexe, exe, sizeof(exe));
    if (!p_exists(exe)) return 0;
    h = CreateFileA(exe, GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h);
  }
  return 0;
}

// ---- pattern walks (UserData / Generated) ----

typedef void (*MatchFn)(const char* path, void* ctx);

// root folder of a data pattern and the part relative to it
static int pattern_root(const char* dir, const char* pat, char* root, const char** rel)
{
  if (starts_i(pat, "%AppData%\\")) {
    *rel = pat + 10;
    return special_dir(0x001A /* CSIDL_APPDATA */, root);
  }
  if (starts_i(pat, "%WinDir%\\")) {
    *rel = pat + 9;
    GetWindowsDirectoryA(root, MAX_PATH);
    return 1;
  }
  *rel = pat;
  s_copy(root, MAX_PATH, dir);
  return dir[0] != 0;
}

static void walk_match(const char* abs, const char* rel, const char* pat, int depth, MatchFn fn, void* ctx)
{
  WIN32_FIND_DATAA fd;
  HANDLE f;
  char spec[MAX_PATH], sub[MAX_PATH], subrel[MAX_PATH];
  if (depth < 0) return;
  p_join(abs, "*.*", spec, sizeof(spec));
  f = FindFirstFileA(spec, &fd);
  if (f == INVALID_HANDLE_VALUE) return;
  do {
    if (!lstrcmpA(fd.cFileName, ".") || !lstrcmpA(fd.cFileName, "..")) continue;
    p_join(abs, fd.cFileName, sub, sizeof(sub));
    if (rel[0]) p_join(rel, fd.cFileName, subrel, sizeof(subrel));
    else s_copy(subrel, sizeof(subrel), fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk_match(sub, subrel, pat, depth - 1, fn, ctx);
    else if (wild_match(pat, subrel)) fn(sub, ctx);
  } while (FindNextFileA(f, &fd));
  FindClose(f);
}

// calls fn for every existing file that matches a UserData/Generated pattern
static void for_matches(const char* dir, const char* pat, MatchFn fn, void* ctx)
{
  char root[MAX_PATH], start[MAX_PATH], prefix[MAX_PATH];
  const char* rel;
  int i, n, cut = 0, depth = 0, deep = 0;
  if (!pattern_root(dir, pat, root, &rel)) return;
  if (!wild_has(rel)) {                          // a plain file: no walk
    p_join(root, rel, start, sizeof(start));
    if (p_exists(start) && !p_is_dir(start)) fn(start, ctx);
    return;
  }
  // walk from the longest folder prefix without wildcards; depth from the pattern (unbounded with **)
  n = lstrlenA(rel);
  for (i = 0; i < n; i++) {
    if (rel[i] == '*' || rel[i] == '?') break;
    if (rel[i] == '\\') cut = i;
  }
  for (i = cut; i < n; i++) if (rel[i] == '\\') depth++;
  if (strstr(rel, "**")) deep = 1;
  lstrcpynA(prefix, rel, cut + 1);
  if (cut) p_join(root, prefix, start, sizeof(start));
  else s_copy(start, sizeof(start), root);
  if (!p_is_dir(start)) return;
  walk_match(start, prefix, rel, deep ? 16 : depth + (cut ? 0 : 0), fn, ctx);
}

static void count_cb(const char* path, void* ctx)
{
  (void)path;
  (*(int*)ctx)++;
}

static void delete_cb(const char* path, void* ctx)
{
  delete_file(path, (int*)ctx);
}

// removes empty folders below abs (deepest first) and abs itself if it ends up empty; never deletes files
static void remove_empty_tree(const char* abs, int self, int depth)
{
  WIN32_FIND_DATAA fd;
  HANDLE f;
  char spec[MAX_PATH], sub[MAX_PATH];
  if (depth > 16 || !p_is_dir(abs)) return;
  p_join(abs, "*.*", spec, sizeof(spec));
  f = FindFirstFileA(spec, &fd);
  if (f != INVALID_HANDLE_VALUE) {
    do {
      if (!lstrcmpA(fd.cFileName, ".") || !lstrcmpA(fd.cFileName, "..")) continue;
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      p_join(abs, fd.cFileName, sub, sizeof(sub));
      remove_empty_tree(sub, 1, depth + 1);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
  }
  if (self && RemoveDirectoryA(abs)) slog("deleted empty folder %s", abs);
}

// %AppData%\X or %WinDir%\X of a token pattern (X = its first folder), for clean-up after deletes
static int token_folder(const char* pat, char* out)
{
  char root[MAX_PATH], first[MAX_PATH];
  const char* rel;
  int i;
  if (!(starts_i(pat, "%AppData%\\") || starts_i(pat, "%WinDir%\\"))) return 0;
  if (!pattern_root("", pat, root, &rel)) return 0;
  for (i = 0; rel[i] && rel[i] != '\\'; i++) if (rel[i] == '*' || rel[i] == '?') return 0;
  if (!rel[i]) return 0;                       // the pattern names a file directly in %AppData%: no own folder
  lstrcpynA(first, rel, i + 1);
  p_join(root, first, out, MAX_PATH);
  return 1;
}

static int file_crc(const char* path, u32* crc)
{
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  u8* buf;
  DWORD n = 0;
  int ok = 1;
  if (h == INVALID_HANDLE_VALUE) return 0;
  buf = (u8*)halloc(65536);
  *crc = 0;
  while (buf && ReadFile(h, buf, 65536, &n, 0) && n) *crc = crc32_update(*crc, buf, n);
  if (!buf) ok = 0;
  hfree(buf);
  CloseHandle(h);
  return ok;
}

int userdata_exists(const char* dir)
{
  Record r;
  int n = 0, i;
  const Manifest* m;
  rec_load(&r, dir);
  m = r.has_manifest ? r.m : &g_m;
  for (i = 0; i < m->nuserdata; i++) for_matches(dir, m->userdata[i], count_cb, &n);
  for (i = 0; i < r.nfiles; i++) {
    char path[MAX_PATH];
    u32 crc;
    if (r.files[i].kind != RF_KEEP) continue;
    p_join(dir, r.files[i].name, path, sizeof(path));
    if (p_exists(path) && (!r.files[i].has_crc || !file_crc(path, &crc) || crc != r.files[i].crc)) n++;
  }
  rec_free(&r);
  return n > 0;
}

// ---- disk space and requirements ----

static void drive_root(const char* dir, char* root)
{
  root[0] = dir[0];
  root[1] = ':';
  root[2] = '\\';
  root[3] = 0;
}

u32 space_needed_kb(const char* dir)
{
  char root[4];
  DWORD spc = 0, bps = 0, fc, tc;
  u32 cl = 4096, kb = 0, i;
  drive_root(dir, root);
  if (GetDiskFreeSpaceA(root, &spc, &bps, &fc, &tc) && spc && bps && spc * bps <= 65536) cl = spc * bps;
  for (i = 0; i < g_pay.nfiles; i++) {
    u32 clusters = (g_pay.files[i].usize + cl - 1) / cl;
    kb += (clusters * (cl / 512) + 1) / 2;
  }
  kb += (g_pay.t.payload_offset + 1023) / 1024 + 16;     // uninstaller + record
  return kb;
}

typedef BOOL (WINAPI* GetDiskFreeSpaceExA_t)(LPCSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);

int space_free_kb(const char* dir, u32* kb)
{
  char root[4];
  GetDiskFreeSpaceExA_t fx;
  DWORD spc = 0, bps = 0, fc = 0, tc = 0;
  drive_root(dir, root);
  // GetDiskFreeSpaceEx exists from Win95 OSR2 on; the old call caps at 2 GB on big FAT32 drives
  fx = (GetDiskFreeSpaceExA_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetDiskFreeSpaceExA");
  if (fx) {
    ULARGE_INTEGER avail, total, freeb;
    if (fx(root, &avail, &total, &freeb)) {
      *kb = avail.HighPart >= 1024 ? 0xFFFFFFFFu : (avail.HighPart << 22) | (avail.LowPart >> 10);
      return 1;
    }
  }
  if (!GetDiskFreeSpaceA(root, &spc, &bps, &fc, &tc)) return 0;
  {
    u32 clkb = spc * bps / 1024;
    *kb = clkb && fc > 0xFFFFFFFFu / clkb ? 0xFFFFFFFFu : fc * (clkb ? clkb : 1);
  }
  return 1;
}

int check_requirements(char* msg, int cap)
{
  char line[300];
  msg[0] = 0;
  if (g_m.req_win) {
    OSVERSIONINFOA vi;
    int have = WIN_ME + 1;          // NT-based Windows counts as newer
    static const char* const names[] = { "", "Windows 95", "Windows 98", "Windows 98 Second Edition", "Windows ME" };
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    GetVersionExA(&vi);
    if (vi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
      if (vi.dwMinorVersion >= 90) have = WIN_ME;
      else if (vi.dwMinorVersion >= 10) have = LOWORD(vi.dwBuildNumber) >= 2222 ? WIN_98SE : WIN_98;
      else have = WIN_95;
    }
    slog("Windows %lu.%lu build %lu: level %d, needs %d", vi.dwMajorVersion, vi.dwMinorVersion,
         LOWORD(vi.dwBuildNumber), have, g_m.req_win);
    if (have < g_m.req_win) {
      wsprintfA(line, "%s needs %s or later. This computer runs %s.\n", g_m.name, names[g_m.req_win], names[have]);
      s_cat(msg, cap, line);
    }
  }
  if (g_m.req_dx) {
    char v[64], sys[MAX_PATH];
    int dx = 0;
    if (reg_get(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\DirectX", "Version", v, sizeof(v)) == 1 && v[0] == '4' &&
        v[1] == '.') {
      int minor = (v[2] - '0') * 10 + (v[3] - '0');      // "4.07.00.0700" = DirectX 7; 4.04 = DirectX 3
      if (minor >= 0 && minor < 100) dx = minor >= 5 ? minor : minor - 1;
    }
    GetSystemDirectoryA(sys, sizeof(sys));
    p_join(sys, "ddraw.dll", sys, sizeof(sys));
    slog("DirectX: registry \"%s\" = %d, ddraw.dll %s; needs %d", v, dx, p_exists(sys) ? "present" : "missing", g_m.req_dx);
    if (dx < g_m.req_dx) {
      if (dx > 0) wsprintfA(line, "%s needs DirectX %d or later. This computer has DirectX %d.\n", g_m.name, g_m.req_dx, dx);
      else if (p_exists(sys))
        wsprintfA(line, "%s needs DirectX %d or later. Setup could not tell which DirectX this computer has.\n",
                  g_m.name, g_m.req_dx);
      else wsprintfA(line, "%s needs DirectX %d or later. Setup found no DirectX on this computer.\n", g_m.name, g_m.req_dx);
      s_cat(msg, cap, line);
    }
  }
  return msg[0] == 0;
}

// ---- shortcuts ----

static const GUID G_CLSID_ShellLink = { 0x00021401, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID G_IID_IShellLinkA = { 0x000214EE, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID G_IID_IPersistFile = { 0x0000010B, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static int make_link(const char* lnk, const MLink* l, const char* dir)
{
  IShellLinkA* sl = 0;
  IPersistFile* pf = 0;
  WCHAR wpath[MAX_PATH];
  char target[MAX_PATH], icon[MAX_PATH];
  HRESULT hr;
  int n;
  p_join(dir, l->target, target, sizeof(target));
  hr = CoCreateInstance(G_CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER, G_IID_IShellLinkA, (void**)&sl);
  if (FAILED(hr) || !sl) { slog("error: IShellLink unavailable (0x%08lX)", (DWORD)hr); return 0; }
  sl->SetPath(target);
  sl->SetWorkingDirectory(dir);
  if (l->args[0]) sl->SetArguments(l->args);
  if (l->desc[0]) sl->SetDescription(l->desc);
  n = lstrlenA(target);
  if (l->icon[0]) {
    char* comma;
    int idx = 0;
    p_join(dir, l->icon, icon, sizeof(icon));
    comma = strrchr(icon, ',');
    if (comma) {
      const char* p = comma + 1;
      *comma = 0;
      while (*p >= '0' && *p <= '9') idx = idx * 10 + (*p++ - '0');
    }
    sl->SetIconLocation(icon, idx);
  } else if (n > 4 && !lstrcmpiA(target + n - 4, ".exe")) {
    sl->SetIconLocation(target, 0);
  }
  hr = sl->QueryInterface(G_IID_IPersistFile, (void**)&pf);
  if (SUCCEEDED(hr) && pf) {
    MultiByteToWideChar(CP_ACP, 0, lnk, -1, wpath, MAX_PATH);
    hr = pf->Save(wpath, TRUE);
    pf->Release();
  }
  sl->Release();
  if (FAILED(hr)) { slog("error: cannot save shortcut %s (0x%08lX)", lnk, (DWORD)hr); return 0; }
  SHChangeNotify(SHCNE_CREATE, SHCNF_PATH, lnk, 0);
  slog("shortcut %s -> %s %s", lnk, target, l->args);
  return 1;
}

static void delete_link(const char* lnk)
{
  if (lnk[0] && lnk[0] != '>' && DeleteFileA(lnk)) {
    SHChangeNotify(SHCNE_DELETE, SHCNF_PATH, lnk, 0);
    slog("deleted shortcut %s", lnk);
  }
}

// deletes the old shortcuts (record + Pica 1.0's StartMenuLnk/DesktopLnk values); folders are handled by the caller
static void links_delete_old(const Record* old)
{
  char key[300], lnk[MAX_PATH];
  int i;
  key_app(key);
  for (i = 0; i < old->nlinks; i++) delete_link(old->links[i]);
  if (reg_get(HKEY_LOCAL_MACHINE, key, "StartMenuLnk", lnk, sizeof(lnk)) == 1) delete_link(lnk);
  if (reg_get(HKEY_LOCAL_MACHINE, key, "DesktopLnk", lnk, sizeof(lnk)) == 1) delete_link(lnk);
  reg_del_value(HKEY_LOCAL_MACHINE, key, "StartMenuLnk");
  reg_del_value(HKEY_LOCAL_MACHINE, key, "DesktopLnk");
}

static void links_remove_folders(const Record* old, const Record* keep)
{
  int i, j;
  for (i = 0; i < old->nlinks; i++) {
    int reused = 0;
    if (old->links[i][0] != '>') continue;
    for (j = 0; keep && j < keep->nlinks; j++) if (!lstrcmpiA(keep->links[j], old->links[i])) reused = 1;
    if (!reused && RemoveDirectoryA(old->links[i] + 1)) {
      SHChangeNotify(SHCNE_RMDIR, SHCNF_PATH, old->links[i] + 1, 0);
      slog("deleted folder %s", old->links[i] + 1);
    }
  }
}

static void rec_add_link(Record* r, const char* path)
{
  if (r->nlinks < (int)(sizeof(r->links) / sizeof(r->links[0]))) s_copy(r->links[r->nlinks++], MAX_PATH, path);
}

static int links_create(const Opts* o, const Record* old, Record* nr)
{
  char folder[MAX_PATH], lnk[MAX_PATH], key[300], name[MAX_PATH];
  int i, ok = 1;
  key_app(key);
  if (o->startmenu && g_m.nlinks) {
    if (!special_dir(CSIDL_PROGRAMS, folder)) { slog("error: Programs folder not found"); return 0; }
    if (g_m.group[0]) {
      char marker[MAX_PATH + 1];
      p_join(folder, g_m.group, folder, sizeof(folder));
      s_copy(marker, sizeof(marker), ">");
      s_cat(marker, sizeof(marker), folder);
      if (!p_is_dir(folder)) {
        if (!CreateDirectoryA(folder, 0)) { slog("error: cannot create %s", folder); return 0; }
        SHChangeNotify(SHCNE_MKDIR, SHCNF_PATH, folder, 0);
        slog("created folder %s", folder);
        rec_add_link(nr, marker);
      } else {
        for (i = 0; i < old->nlinks; i++) if (!lstrcmpiA(old->links[i], marker)) rec_add_link(nr, marker);
      }
    }
    for (i = 0; i < g_m.nlinks; i++) {
      s_copy(name, sizeof(name), g_m.links[i].name);
      s_cat(name, sizeof(name), ".lnk");
      p_join(folder, name, lnk, sizeof(lnk));
      if (!make_link(lnk, &g_m.links[i], o->dir)) { ok = 0; continue; }
      rec_add_link(nr, lnk);
      if (i == 0) reg_set(HKEY_LOCAL_MACHINE, key, "StartMenuLnk", lnk);
    }
  }
  if (o->desktop && g_m.ndlinks) {
    if (!special_dir(CSIDL_DESKTOPDIRECTORY, folder)) { slog("error: Desktop folder not found"); return 0; }
    for (i = 0; i < g_m.ndlinks; i++) {
      s_copy(name, sizeof(name), g_m.dlinks[i].name);
      s_cat(name, sizeof(name), ".lnk");
      p_join(folder, name, lnk, sizeof(lnk));
      if (!make_link(lnk, &g_m.dlinks[i], o->dir)) { ok = 0; continue; }
      rec_add_link(nr, lnk);
      if (i == 0) reg_set(HKEY_LOCAL_MACHINE, key, "DesktopLnk", lnk);
    }
  }
  return ok;
}

void notify_shell()
{
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
}

// ---- install ----

static void step(ProgressFn fn, void* ctx, int pct, const char* text)
{
  slog("[%d%%] %s", pct, text);
  if (fn) fn(pct, text, ctx);
}

static int write_file(const char* path, const u8* data, u32 size)
{
  HANDLE h;
  DWORD n = 0;
  int ok;
  SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
  h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  if (h == INVALID_HANDLE_VALUE) return 0;
  ok = WriteFile(h, data, size, &n, 0) && n == size;
  if (!CloseHandle(h)) ok = 0;
  if (!ok) DeleteFileA(path);
  return ok;
}

struct Plan {
  int idx;                // payload entry, -1 = the uninstaller
  char kind;              // RF_PROGRAM / RF_KEEP
  int write;              // 0 = keep file that exists already
  int temp_ok;            // phase 1 wrote the temp file
  u32 size, crc;
  char name[PF_MAX_NAME + 1];
  char target[MAX_PATH];
  char tmp[MAX_PATH];
};

static void temp_name(const char* target, int i, char* out)
{
  char dir[MAX_PATH], nm[16];
  char* s;
  s_copy(dir, sizeof(dir), target);
  s = strrchr(dir, '\\');
  if (s) *s = 0;
  wsprintfA(nm, "~S98%04X.TMP", i & 0xFFFF);
  p_join(dir, nm, out, MAX_PATH);
}

static int matches_any(char pats[][M_PATH], int n, const char* name)
{
  int i;
  for (i = 0; i < n; i++) if (!starts_i(pats[i], "%") && wild_match(pats[i], name)) return 1;
  return 0;
}

// creates the folders of a relative file path; newly created ones go into the record (and the undo list)
static int make_parent_dirs(const char* dir, const char* name, Record* nr, Record* made)
{
  char rel[PF_MAX_NAME + 1], abs[MAX_PATH];
  int i;
  for (i = 0; name[i]; i++) {
    if (name[i] != '\\') continue;
    lstrcpynA(rel, name, i + 1);
    p_join(dir, rel, abs, sizeof(abs));
    if (p_is_dir(abs)) continue;
    if (!CreateDirectoryA(abs, 0)) { slog("error: cannot create folder %s (%lu)", abs, GetLastError()); return 0; }
    slog("created folder %s", abs);
    rec_add_dir(nr, rel);
    rec_add_dir(made, rel);
  }
  return 1;
}

// removes the recorded folders that are empty, deepest first; removed ones leave the record
static void remove_dirs(const char* dir, Record* r)
{
  char* state;
  int k, i, n = 0;
  if (!r->ndirs) return;
  state = (char*)halloc((u32)r->ndirs);          // 0 = to do, 1 = kept, 2 = removed
  if (!state) return;
  for (i = 0; i < r->ndirs; i++) state[i] = 0;
  for (k = 0; k < r->ndirs; k++) {
    int best = -1, bl = -1;
    char abs[MAX_PATH];
    for (i = 0; i < r->ndirs; i++) if (!state[i] && lstrlenA(r->dirs[i]) > bl) { best = i; bl = lstrlenA(r->dirs[i]); }
    if (best < 0) break;
    p_join(dir, r->dirs[best], abs, sizeof(abs));
    state[best] = 1;
    if (!p_is_dir(abs)) state[best] = 2;
    else if (RemoveDirectoryA(abs)) { slog("deleted folder %s", abs); state[best] = 2; }
  }
  for (i = 0; i < r->ndirs; i++) {
    if (state[i] == 2) continue;
    if (n != i) s_copy(r->dirs[n], PF_MAX_NAME + 1, r->dirs[i]);
    n++;
  }
  r->ndirs = n;
  hfree(state);
}

int inst_run(const Opts* o, ProgressFn fn, void* ctx, char* err, int errcap)
{
  Record old, nr, made;
  Plan* plan = 0;
  u8 *buf = 0, *cbuf = 0;
  HANDLE* lock = 0;
  char path[MAX_PATH], key[300], ukey[300], cmd[600], recname[64], text[200];
  int i, np = 0, reboot = 0, ok = 1, rc = RC_FAIL;
  u32 done = 0, total = 0;
  char* renames = 0;          // "8.3 path|long path" lines: WININIT.INI brings files back under their 8.3 names
  u32 rnlen = 0;
  err[0] = 0;
  wininit_reset();
  rec_init(&old);
  rec_init(&nr);
  rec_init(&made);
  key_app(key);
  key_uninst(ukey);
  m_record_name(&g_m, recname);
  slog("install %s %s into %s (start menu %d, desktop %d, context menu %d)", g_m.name, g_m.version, o->dir,
       o->startmenu, o->desktop, o->context);

  step(fn, ctx, 2, "Creating the folder...");
  if (!make_dirs(o->dir)) {
    wsprintfA(err, "Setup could not create the folder %s.", o->dir);
    return RC_FAIL;
  }
  rec_load(&old, o->dir);
  for (i = 0; i < old.ndirs; i++) rec_add_dir(&nr, old.dirs[i]);

  // plan: every payload file + the uninstaller
  plan = (Plan*)halloc((g_pay.nfiles + 1) * (u32)sizeof(Plan));
  lock = (HANDLE*)halloc((g_pay.nfiles + 1) * (u32)sizeof(HANDLE));
  buf = (u8*)halloc(g_pay.max_usize + 1);
  cbuf = (u8*)halloc(g_pay.max_csize + 1);
  if (!plan || !lock || !buf || !cbuf) { s_copy(err, errcap, "Not enough memory."); goto out; }
  for (i = 0; i <= (int)g_pay.nfiles; i++) {
    Plan* p = &plan[np];
    memset(p, 0, sizeof(*p));
    lock[np] = INVALID_HANDLE_VALUE;
    if (i < (int)g_pay.nfiles) {
      p->idx = i;
      s_copy(p->name, sizeof(p->name), g_pay.files[i].name);
      p->kind = (g_pay.files[i].flags & PF_KEEP) ? RF_KEEP : RF_PROGRAM;
      p->size = g_pay.files[i].usize;
      p->crc = g_pay.files[i].crc;
    } else {
      p->idx = -1;
      s_copy(p->name, sizeof(p->name), g_m.uninstaller);
      p->kind = RF_PROGRAM;
      p->size = g_pay.t.payload_offset;
    }
    p_join(o->dir, p->name, p->target, sizeof(p->target));
    if (lstrlenA(o->dir) + 1 + lstrlenA(p->name) >= MAX_PATH - 1) {
      wsprintfA(err, "The folder path is too long for %s.", p->name);
      goto out;
    }
    temp_name(p->target, np, p->tmp);
    p->write = !(p->kind == RF_KEEP && p_exists(p->target));
    if (p->write) total += p->size;
    else slog("kept %s (exists)", p->target);
    np++;
  }

  // phase 1: every new file next to its target under a temp name, CRC-checked (a damaged setup or a full disk
  // changes nothing)
  for (i = 0; i < np && ok; i++) {
    Plan* p = &plan[i];
    int r;
    if (!p->write) continue;
    if (!make_parent_dirs(o->dir, p->name, &nr, &made)) {
      wsprintfA(err, "Setup could not create a folder in %s.", o->dir);
      ok = 0;
      break;
    }
    wsprintfA(text, "Copying files... %s", p->name);
    if (fn) fn(5 + (total ? MulDiv((int)done, 70, (int)total) : 70), text, ctx);
    if (p->idx < 0) {
      r = pay_write_uninstaller(p->tmp, &p->crc) ? 1 : 0;
    } else {
      r = pay_extract(p->idx, buf, cbuf);
      if (r == 1 && !write_file(p->tmp, buf, p->size)) r = 0;
    }
    if (r != 1) {
      slog("error: %s: %s (%lu)", p->name, r < 0 ? "CRC mismatch: the setup file is damaged" : "cannot write",
           GetLastError());
      if (r < 0) wsprintfA(err, "This setup file is damaged (%s fails its check). Please download or copy it again.",
                           p->name);
      else wsprintfA(err, "Setup could not write to %s. Is the disk full or write-protected?", o->dir);
      ok = 0;
      break;
    }
    p->temp_ok = 1;
    done += p->size;
  }
  if (!ok) {
    for (i = 0; i < np; i++) if (plan[i].temp_ok) DeleteFileA(plan[i].tmp);
    remove_dirs(o->dir, &made);
    goto out;
  }

  // phase 2: swap them in; a file in use is replaced by WININIT.INI at the next start
  step(fn, ctx, 76, "Replacing program files...");
  for (i = 0; i < np; i++) {
    Plan* p = &plan[i];
    if (!p->write) continue;
    if (o->locktest && p_exists(p->target))       // test switch: hold the old file open like a running program
      lock[i] = CreateFileA(p->target, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (p_exists(p->target)) {
      SetFileAttributesA(p->target, FILE_ATTRIBUTE_NORMAL);
      if (!DeleteFileA(p->target)) {
        char sp[MAX_PATH], st[MAX_PATH];
        if (!GetShortPathNameA(p->target, sp, sizeof(sp)) || !GetShortPathNameA(p->tmp, st, sizeof(st)) ||
            !wininit_line("NUL", sp) || !wininit_line(sp, st)) {
          wsprintfA(err, "%s is in use and could not be replaced.", p->target);
          ok = 0;
          break;
        }
        slog("in use: %s (replaced when Windows restarts)", p->target);
        reboot = 1;
        if (lstrcmpiA(sp, p->target) != 0) {      // a long name: put it back after the restart (RunOnce)
          u32 add = (u32)lstrlenA(sp) + (u32)lstrlenA(p->target) + 4;
          char* nb = (char*)halloc(rnlen + add + 1);
          if (nb) {
            if (renames) { memcpy(nb, renames, rnlen); hfree(renames); }
            renames = nb;
            wsprintfA(renames + rnlen, "%s|%s\r\n", sp, p->target);
            rnlen += (u32)lstrlenA(renames + rnlen);
          }
        }
        continue;
      }
    }
    if (!MoveFileA(p->tmp, p->target)) {
      wsprintfA(err, "Setup could not create %s.", p->target);
      ok = 0;
      break;
    }
  }
  for (i = 0; i < np; i++) if (lock[i] != INVALID_HANDLE_VALUE) CloseHandle(lock[i]);
  if (!ok) {
    for (i = 0; i < np; i++) if (plan[i].temp_ok && p_exists(plan[i].tmp)) DeleteFileA(plan[i].tmp);
    goto out;
  }
  slog("installed %d files (%lu bytes)", np, done);

  // the new record: program files with their CRC, keep files (with the CRC they had when Setup wrote them)
  for (i = 0; i < np; i++) {
    Plan* p = &plan[i];
    if (p->write) rec_add_file(&nr, p->name, p->kind, 1, p->crc);
    else {
      int k = rec_find_file(&old, p->name);
      if (k >= 0 && old.files[k].kind == RF_KEEP) rec_add_file(&nr, p->name, RF_KEEP, old.files[k].has_crc, old.files[k].crc);
      else rec_add_file(&nr, p->name, RF_KEEP, 0, 0);
    }
  }
  // files of the old version that this one doesn't ship: program files go, keep files stay (still recorded)
  step(fn, ctx, 80, "Removing obsolete files...");
  for (i = 0; i < old.nfiles; i++) {
    const RecFile* f = &old.files[i];
    if (rec_find_file(&nr, f->name) >= 0 || !lstrcmpiA(f->name, recname)) continue;
    if (f->kind == RF_KEEP || matches_any(g_m.userdata, g_m.nuserdata, f->name)) {
      rec_add_file(&nr, f->name, RF_KEEP, f->has_crc, f->crc);
      continue;
    }
    p_join(o->dir, f->name, path, sizeof(path));
    delete_file(path, &reboot);
  }
  remove_dirs(o->dir, &nr);
  if (reboot && !wininit_flush()) { s_copy(err, errcap, "Setup could not write WININIT.INI."); goto out; }

  // registry
  step(fn, ctx, 84, "Writing the registry...");
  {
    char files[8200], types[1200];
    int fits = 1;
    files[0] = 0;
    for (i = 0; i < nr.nfiles; i++) {
      if (nr.files[i].kind != RF_PROGRAM) continue;
      if (lstrlenA(files) + lstrlenA(nr.files[i].name) + 2 >= (int)sizeof(files)) { fits = 0; break; }
      if (files[0]) s_cat(files, sizeof(files), ";");
      s_cat(files, sizeof(files), nr.files[i].name);
    }
    ok = reg_set(HKEY_LOCAL_MACHINE, key, "InstallDir", o->dir) && reg_set(HKEY_LOCAL_MACHINE, key, "Version", g_m.version) &&
         reg_set_dw(HKEY_LOCAL_MACHINE, key, "StartMenu", (DWORD)o->startmenu) &&
         reg_set_dw(HKEY_LOCAL_MACHINE, key, "Desktop", (DWORD)o->desktop);
    if (ok && fits) ok = reg_set(HKEY_LOCAL_MACHINE, key, "Files", files);    // Pica 1.0 compatible list
    else reg_del_value(HKEY_LOCAL_MACHINE, key, "Files");
    if (ok && g_m.has_types) {
      types[0] = 0;
      for (i = 0; i < g_m.next; i++) {
        if (!o->types[i]) continue;
        if (types[0]) s_cat(types, sizeof(types), ";");
        s_cat(types, sizeof(types), g_m.exts[i].ext);
      }
      ok = reg_set(HKEY_LOCAL_MACHINE, key, "Types", types);
    }
    if (ok && g_m.has_context) ok = reg_set_dw(HKEY_LOCAL_MACHINE, key, "ContextMenu", (DWORD)o->context);
  }
  if (ok && (g_m.has_types || g_m.has_context)) {
    static const int none[M_MAX_EXT] = { 0 };
    step(fn, ctx, 88, "Setting up file types...");
    our_command(o->dir, cmd, sizeof(cmd));
    ok = context_set(&g_m, o->context, cmd);
    if (ok) ok = assoc_apply(&g_m, o->dir, g_m.has_types ? o->types : none);
    legacy_cleanup(&g_m);
    created_cleanup();
  }

  step(fn, ctx, 92, "Creating shortcuts...");
  links_delete_old(&old);
  if (ok) ok = links_create(o, &old, &nr);
  links_remove_folders(&old, &nr);

  if (renames) {
    char run[MAX_PATH + 40], vname[80];
    p_join(o->dir, g_m.uninstaller, path, sizeof(path));
    wsprintfA(run, "\"%s\" /_FIXNAMES", path);
    wsprintfA(vname, "Setup98_%s", g_m.id);
    reg_set(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", vname, run);
    slog("RunOnce %s: %s (restores long file names after the restart)", vname, run);
  }
  if (ok && !rec_save(&nr, &g_m, o, renames)) {
    wsprintfA(err, "Setup could not write %s in %s.", recname, o->dir);
    ok = 0;
  }

  step(fn, ctx, 96, "Registering the uninstaller...");
  if (ok) {
    char dn[200], q[MAX_PATH + 4];
    p_join(o->dir, g_m.uninstaller, path, sizeof(path));
    s_copy(q, sizeof(q), "\"");
    s_cat(q, sizeof(q), path);
    s_cat(q, sizeof(q), "\"");
    wsprintfA(dn, "%s %s", g_m.name, g_m.version);
    ok = reg_set(HKEY_LOCAL_MACHINE, ukey, "DisplayName", dn) &&
         reg_set(HKEY_LOCAL_MACHINE, ukey, "DisplayVersion", g_m.version) &&
         reg_set(HKEY_LOCAL_MACHINE, ukey, "UninstallString", q) &&
         reg_set(HKEY_LOCAL_MACHINE, ukey, "InstallLocation", o->dir);
    p_join(o->dir, g_m.mainexe, q, sizeof(q));
    s_cat(q, sizeof(q), ",0");
    if (ok) ok = reg_set(HKEY_LOCAL_MACHINE, ukey, "DisplayIcon", q);
    if (ok && g_m.publisher[0]) ok = reg_set(HKEY_LOCAL_MACHINE, ukey, "Publisher", g_m.publisher);
    if (ok && g_m.url[0]) ok = reg_set(HKEY_LOCAL_MACHINE, ukey, "URLInfoAbout", g_m.url);
    reg_set_dw(HKEY_LOCAL_MACHINE, ukey, "NoModify", 1);
    reg_set_dw(HKEY_LOCAL_MACHINE, ukey, "NoRepair", 1);
  }
  notify_shell();
  if (!ok) {
    if (!err[0]) s_copy(err, errcap, "Setup could not write the registry or create the shortcuts (see the log).");
    goto out;
  }
  step(fn, ctx, 100, reboot ? "Done; restart Windows to finish." : "Done.");
  slog("install finished: %s", reboot ? "restart needed" : "ok");
  rc = reboot ? RC_REBOOT : RC_OK;
out:
  hfree(renames);
  hfree(plan);
  hfree(lock);
  hfree(buf);
  hfree(cbuf);
  rec_free(&old);
  rec_free(&nr);
  rec_free(&made);
  return rc;
}

// ---- uninstall ----

int uninst_run(const char* dir, int purge, int defer_self, int* kept_folder)
{
  Record r;
  const Manifest* m;
  char path[MAX_PATH], key[300], ukey[300], cmd[600], recname[64], tok[MAX_PATH];
  static const int none[M_MAX_EXT] = { 0 };
  int i, reboot = 0, found;
  wininit_reset();
  key_app(key);
  key_uninst(ukey);
  slog("uninstall from %s (delete user data %d)", dir, purge);
  found = rec_load(&r, dir);
  m = r.has_manifest ? r.m : &g_m;
  m_record_name(&g_m, recname);
  our_command(dir, cmd, sizeof(cmd));

  // registry first: associations back to what they were, then our own keys
  assoc_apply(m, dir, none);
  context_set(m, 0, cmd);
  legacy_cleanup(m);
  created_cleanup();
  links_delete_old(&r);
  links_remove_folders(&r, 0);
  reg_del_tree(HKEY_LOCAL_MACHINE, ukey);
  reg_del_tree(HKEY_LOCAL_MACHINE, key);
  {
    char vname[80];
    wsprintfA(vname, "Setup98_%s", g_m.id);
    reg_del_value(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", vname);
  }
  notify_shell();

  // program files and unchanged keep files
  for (i = 0; i < r.nfiles; i++) {
    const RecFile* f = &r.files[i];
    p_join(dir, f->name, path, sizeof(path));
    if (f->kind == RF_PROGRAM) {
      if (defer_self && !lstrcmpiA(f->name, g_m.uninstaller)) continue;
      delete_file(path, &reboot);
    } else if (p_exists(path)) {
      u32 crc;
      if (purge || (f->has_crc && file_crc(path, &crc) && crc == f->crc)) delete_file(path, &reboot);
      else slog("kept %s (changed since it was installed)", path);
    }
  }
  if (!found) {          // nothing recorded at all: at least the program and the uninstaller
    p_join(dir, m->mainexe, path, sizeof(path));
    delete_file(path, &reboot);
    if (!defer_self) { p_join(dir, g_m.uninstaller, path, sizeof(path)); delete_file(path, &reboot); }
  }
  // leftovers of an interrupted install
  for_matches(dir, "**\\~S98????.TMP", delete_cb, &reboot);
  for (i = 0; i < m->ngenerated; i++) {
    for_matches(dir, m->generated[i], delete_cb, &reboot);
    if (token_folder(m->generated[i], tok) && RemoveDirectoryA(tok)) slog("deleted empty folder %s", tok);
  }
  if (purge) {
    for (i = 0; i < m->nuserdata; i++) for_matches(dir, m->userdata[i], delete_cb, &reboot);
    for (i = 0; i < m->nuserdata; i++) if (token_folder(m->userdata[i], tok)) remove_empty_tree(tok, 1, 0);
  }
  p_join(dir, recname, path, sizeof(path));
  delete_file(path, &reboot);
  remove_dirs(dir, &r);
  if (purge) remove_empty_tree(dir, 0, 0);
  wininit_flush();
  rec_free(&r);
  if (!defer_self) uninst_finish(dir);
  if (kept_folder) *kept_folder = p_is_dir(dir);
  slog("uninstall finished: %s", reboot ? "restart needed" : "ok");
  return reboot ? RC_REBOOT : RC_OK;
}

int uninst_finish(const char* dir)
{
  char path[MAX_PATH];
  int reboot = 0, tries;
  p_join(dir, g_m.uninstaller, path, sizeof(path));
  for (tries = 0; tries < 50 && p_exists(path); tries++) {
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileA(path)) { slog("deleted %s", path); break; }
    Sleep(100);
  }
  if (p_exists(path)) delete_file(path, &reboot);
  wininit_flush();
  if (RemoveDirectoryA(dir)) slog("deleted folder %s", dir);
  else if (p_is_dir(dir)) slog("folder kept (not empty): %s", dir);
  return reboot ? RC_REBOOT : RC_OK;
}
