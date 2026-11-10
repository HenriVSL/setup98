// util.cpp - Setup98 stub helpers: strings, paths, log, registry, WININIT.INI. Generalised from Pica's setup
// (editor/setup/inst.cpp). ANSI APIs only; everything Win98-safe.
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include <stdarg.h>
#include "s98.h"

// ---- strings and paths ----

void s_copy(char* dst, int cap, const char* src)
{
  if (cap <= 0) return;
  lstrcpynA(dst, src ? src : "", cap);
}

void s_cat(char* dst, int cap, const char* src)
{
  int n = lstrlenA(dst);
  if (n < cap - 1) lstrcpynA(dst + n, src, cap - n);
}

int starts_i(const char* s, const char* prefix)
{
  int n = lstrlenA(prefix);
  return lstrlenA(s) >= n && CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, s, n, prefix, n) == 2;
}

void p_join(const char* dir, const char* name, char* out, int cap)
{
  int n;
  char tmp[MAX_PATH * 2];
  s_copy(tmp, sizeof(tmp), dir);
  n = lstrlenA(tmp);
  if (n > 0 && tmp[n - 1] != '\\') s_cat(tmp, sizeof(tmp), "\\");
  s_cat(tmp, sizeof(tmp), name);
  s_copy(out, cap, tmp);
}

int p_exists(const char* path)
{
  return GetFileAttributesA(path) != 0xFFFFFFFF;
}

int p_is_dir(const char* path)
{
  DWORD a = GetFileAttributesA(path);
  return a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void p_module_dir(char* out, int cap)
{
  char buf[MAX_PATH];
  char* p;
  GetModuleFileNameA(0, buf, sizeof(buf));
  p = strrchr(buf, '\\');
  if (p) *p = 0;
  s_copy(out, cap, buf);
}

int p_same(const char* a, const char* b)
{
  char sa[MAX_PATH], sb[MAX_PATH];
  if (!GetShortPathNameA(a, sa, sizeof(sa))) s_copy(sa, sizeof(sa), a);
  if (!GetShortPathNameA(b, sb, sizeof(sb))) s_copy(sb, sizeof(sb), b);
  return lstrcmpiA(sa, sb) == 0;
}

int p_normalize_dir(const char* in, char* out, int cap)
{
  char buf[MAX_PATH], full[MAX_PATH];
  char* fp;
  int n;
  s_copy(buf, sizeof(buf), in);
  n = lstrlenA(buf);
  while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '"')) buf[--n] = 0;
  fp = buf;
  while (*fp == ' ' || *fp == '"') fp++;
  if (!(fp[0] && fp[1] == ':' && fp[2] == '\\')) return 0;     // absolute, on a drive
  if (!GetFullPathNameA(fp, sizeof(full), full, &fp)) return 0;
  n = lstrlenA(full);
  while (n > 3 && full[n - 1] == '\\') full[--n] = 0;
  if (n <= 3) return 0;                                        // not a drive root
  if (n > MAX_PATH - 20) return 0;
  s_copy(out, cap, full);
  return 1;
}

int make_dirs(const char* dir)
{
  char buf[MAX_PATH];
  int i, n;
  s_copy(buf, sizeof(buf), dir);
  n = lstrlenA(buf);
  for (i = 3; i <= n; i++) {
    if (buf[i] == '\\' || buf[i] == 0) {
      char c = buf[i];
      buf[i] = 0;
      if (!p_is_dir(buf) && !CreateDirectoryA(buf, 0)) {
        slog("error: cannot create folder %s (%lu)", buf, GetLastError());
        return 0;
      }
      buf[i] = c;
    }
  }
  return 1;
}

void expand_folder(const char* tmpl, char* out, int cap)
{
  char base[MAX_PATH];
  if (starts_i(tmpl, "%ProgramFiles%")) {
    if (reg_get(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion", "ProgramFilesDir", base,
                sizeof(base)) != 1 || !base[0])
      s_copy(base, sizeof(base), "C:\\Program Files");
    s_cat(base, sizeof(base), tmpl + 14);
  } else if (starts_i(tmpl, "%WinDrive%")) {
    GetWindowsDirectoryA(base, sizeof(base));
    base[2] = 0;
    s_cat(base, sizeof(base), tmpl + 10);
  } else {
    s_copy(base, sizeof(base), tmpl);
  }
  s_copy(out, cap, base);
}

int special_dir(int csidl, char* out)
{
  LPITEMIDLIST pidl = 0;
  LPMALLOC m = 0;
  int ok;
  out[0] = 0;
  if (SHGetSpecialFolderLocation(0, csidl, &pidl) != NOERROR || !pidl) return 0;
  ok = SHGetPathFromIDListA(pidl, out) ? 1 : 0;
  if (SHGetMalloc(&m) == NOERROR && m) {
    m->Free(pidl);
    m->Release();
  }
  return ok && out[0];
}

// ---- log ----

static HANDLE g_log = INVALID_HANDLE_VALUE;

void log_open(const char* path, int truncate)
{
  SYSTEMTIME st;
  g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, 0, truncate ? CREATE_ALWAYS : OPEN_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, 0);
  if (g_log == INVALID_HANDLE_VALUE) return;
  SetFilePointer(g_log, 0, 0, FILE_END);
  GetLocalTime(&st);
  slog("---- %s setup (Setup98 %s), %04u-%02u-%02u %02u:%02u:%02u, pid %08lX", g_m.name[0] ? g_m.name : "?",
       S98_VERSION_STR, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
}

void log_close()
{
  if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
  g_log = INVALID_HANDLE_VALUE;
}

void slog(const char* fmt, ...)
{
  char buf[1100];
  va_list ap;
  DWORD n;
  if (g_log == INVALID_HANDLE_VALUE) return;
  va_start(ap, fmt);
  wvsprintfA(buf, fmt, ap);      // writes at most 1024 characters
  va_end(ap);
  s_cat(buf, sizeof(buf), "\r\n");
  WriteFile(g_log, buf, lstrlenA(buf), &n, 0);
}

// ---- registry ----

const char* root_name(HKEY root)
{
  return root == HKEY_CLASSES_ROOT ? "HKCR" : "HKLM";
}

void key_app(char* out)
{
  s_copy(out, 300, "Software\\");
  s_cat(out, 300, g_m.id);
}

void key_uninst(char* out)
{
  s_copy(out, 300, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\");
  s_cat(out, 300, g_m.id);
}

int reg_get(HKEY root, const char* key, const char* name, char* out, int cap)
{
  HKEY h;
  DWORD type = 0, size = (DWORD)cap;
  LONG r;
  out[0] = 0;
  if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return -1;
  r = RegQueryValueExA(h, (LPSTR)name, 0, &type, (BYTE*)out, &size);
  RegCloseKey(h);
  if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) { out[0] = 0; return 0; }
  out[cap - 1] = 0;
  return 1;
}

DWORD reg_get_dw(HKEY root, const char* key, const char* name, DWORD def)
{
  HKEY h;
  DWORD v = def, type = 0, size = sizeof(v);
  if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return def;
  if (RegQueryValueExA(h, (LPSTR)name, 0, &type, (BYTE*)&v, &size) != ERROR_SUCCESS || type != REG_DWORD) v = def;
  RegCloseKey(h);
  return v;
}

int reg_key_exists(HKEY root, const char* key)
{
  HKEY h;
  if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
  RegCloseKey(h);
  return 1;
}

int reg_key_empty(HKEY root, const char* key)
{
  HKEY h;
  DWORD nsub = 0, nval = 0;
  LONG r;
  if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
  r = RegQueryInfoKeyA(h, 0, 0, 0, &nsub, 0, 0, &nval, 0, 0, 0, 0);
  RegCloseKey(h);
  return r == ERROR_SUCCESS && nsub == 0 && nval == 0;
}

int reg_set(HKEY root, const char* key, const char* name, const char* val)
{
  HKEY h;
  LONG r;
  if (RegCreateKeyExA(root, key, 0, 0, 0, KEY_WRITE, 0, &h, 0) != ERROR_SUCCESS) {
    slog("error: cannot open %s\\%s", root_name(root), key);
    return 0;
  }
  r = RegSetValueExA(h, name, 0, REG_SZ, (const BYTE*)val, lstrlenA(val) + 1);
  RegCloseKey(h);
  if (r != ERROR_SUCCESS) slog("error: cannot write %s\\%s [%s]", root_name(root), key, name ? name : "");
  return r == ERROR_SUCCESS;
}

int reg_set_dw(HKEY root, const char* key, const char* name, DWORD v)
{
  HKEY h;
  LONG r;
  if (RegCreateKeyExA(root, key, 0, 0, 0, KEY_WRITE, 0, &h, 0) != ERROR_SUCCESS) return 0;
  r = RegSetValueExA(h, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
  RegCloseKey(h);
  return r == ERROR_SUCCESS;
}

void reg_del_value(HKEY root, const char* key, const char* name)
{
  HKEY h;
  if (RegOpenKeyExA(root, key, 0, KEY_WRITE, &h) != ERROR_SUCCESS) return;
  RegDeleteValueA(h, name);
  RegCloseKey(h);
}

// deletes key and everything below it (own recursion: NT's RegDeleteKey refuses keys with subkeys)
void reg_del_tree(HKEY root, const char* key)
{
  HKEY h;
  char sub[300], path[600];
  DWORD n;
  if (RegOpenKeyExA(root, key, 0, KEY_READ | KEY_WRITE, &h) != ERROR_SUCCESS) return;
  for (;;) {
    n = sizeof(sub);
    if (RegEnumKeyExA(h, 0, sub, &n, 0, 0, 0, 0) != ERROR_SUCCESS) break;
    s_copy(path, sizeof(path), key);
    s_cat(path, sizeof(path), "\\");
    s_cat(path, sizeof(path), sub);
    if (lstrlenA(path) >= 590) break;
    reg_del_tree(root, path);
    if (reg_key_exists(root, path)) break;      // could not delete: avoid looping forever
  }
  RegCloseKey(h);
  if (RegDeleteKeyA(root, key) == ERROR_SUCCESS) slog("deleted %s\\%s", root_name(root), key);
}

// deletes key only if it has no values and no subkeys (Win9x RegDeleteKey would delete a whole tree)
int reg_del_if_empty(HKEY root, const char* key)
{
  if (!reg_key_empty(root, key)) return 0;
  if (RegDeleteKeyA(root, key) != ERROR_SUCCESS) return 0;
  slog("deleted empty %s\\%s", root_name(root), key);
  return 1;
}

int reg_value_names(HKEY root, const char* key, char names[][260], int max)
{
  HKEY h;
  int n = 0;
  DWORD i, len;
  if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
  for (i = 0; n < max; i++) {
    len = 260;
    if (RegEnumValueA(h, i, names[n], &len, 0, 0, 0, 0) != ERROR_SUCCESS) break;
    n++;
  }
  RegCloseKey(h);
  return n;
}

// ---- WININIT.INI ----
// [rename] lines are collected and written in one go. Win9x processes them at the next start: "NUL=<file>" deletes,
// "<dst>=<src>" moves. Both paths must be 8.3. WritePrivateProfileString can't write duplicate NUL= keys, so the file
// is edited by hand.

static char g_wi[8192];

void wininit_reset()
{
  g_wi[0] = 0;
}

int wininit_line(const char* dst, const char* src)
{
  char line[MAX_PATH * 2 + 8];
  s_copy(line, sizeof(line), dst);
  s_cat(line, sizeof(line), "=");
  s_cat(line, sizeof(line), src);
  s_cat(line, sizeof(line), "\r\n");
  if (lstrlenA(g_wi) + lstrlenA(line) >= (int)sizeof(g_wi)) return 0;
  s_cat(g_wi, sizeof(g_wi), line);
  slog("WININIT.INI [rename] %s=%s", dst, src);
  return 1;
}

int wininit_flush()
{
  char path[MAX_PATH];
  HANDLE h;
  char* old = 0;
  char* out;
  DWORD size = 0, n = 0, pos = 0, outlen = 0;
  int ok;
  if (!g_wi[0]) return 1;
  GetWindowsDirectoryA(path, sizeof(path));
  p_join(path, "WININIT.INI", path, sizeof(path));
  h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (h != INVALID_HANDLE_VALUE) {
    size = GetFileSize(h, 0);
    if (size > 0 && size < 65536) {
      old = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
      if (old && ReadFile(h, old, size, &n, 0)) old[n] = 0;
      else n = 0;
    }
    CloseHandle(h);
  }
  if (old && n) {          // drop lines the file already has (a second run before the restart)
    static char keep[sizeof(g_wi)];
    char* l = g_wi;
    keep[0] = 0;
    while (*l) {
      char* e = strchr(l, '\n');
      char line[MAX_PATH * 2 + 8];
      int len = e ? (int)(e - l) + 1 : lstrlenA(l);
      lstrcpynA(line, l, len + 1 < (int)sizeof(line) ? len + 1 : (int)sizeof(line));
      if (!strstr(old, line)) s_cat(keep, sizeof(keep), line);
      l += len;
    }
    s_copy(g_wi, sizeof(g_wi), keep);
    if (!g_wi[0]) { HeapFree(GetProcessHeap(), 0, old); return 1; }
  }
  out = (char*)HeapAlloc(GetProcessHeap(), 0, n + sizeof(g_wi) + 32);
  if (!out) return 0;
  // insert our lines right after an existing [rename] header, else append the section
  if (old && n) {
    DWORD i = 0;
    while (i < n) {
      DWORD e = i;
      while (e < n && old[e] != '\n') e++;
      if (e - i >= 8 && CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, old + i, 8, "[rename]", 8) == 2) {
        pos = e < n ? e + 1 : n;
        break;
      }
      i = e + 1;
    }
  }
  if (pos) {
    memcpy(out, old, pos);
    outlen = pos;
    if (old[pos - 1] != '\n') { out[outlen++] = '\r'; out[outlen++] = '\n'; }
    memcpy(out + outlen, g_wi, lstrlenA(g_wi));
    outlen += lstrlenA(g_wi);
    memcpy(out + outlen, old + pos, n - pos);
    outlen += n - pos;
  } else {
    if (old && n) {
      memcpy(out, old, n);
      outlen = n;
      if (old[n - 1] != '\n') { out[outlen++] = '\r'; out[outlen++] = '\n'; }
    }
    memcpy(out + outlen, "[rename]\r\n", 10);
    outlen += 10;
    memcpy(out + outlen, g_wi, lstrlenA(g_wi));
    outlen += lstrlenA(g_wi);
  }
  h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
  ok = h != INVALID_HANDLE_VALUE && WriteFile(h, out, outlen, &n, 0) && n == outlen;
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  if (old) HeapFree(GetProcessHeap(), 0, old);
  HeapFree(GetProcessHeap(), 0, out);
  if (!ok) slog("error: cannot write %s", path);
  g_wi[0] = 0;
  return ok;
}

int wininit_delete_later(const char* path)
{
  char sp[MAX_PATH];
  if (!GetShortPathNameA(path, sp, sizeof(sp))) return 0;
  return wininit_line("NUL", sp) && wininit_flush();
}

// deletes a file now, or (in use) when Windows restarts; *reboot set then. Lines are flushed by the caller.
void delete_file(const char* path, int* reboot)
{
  char sp[MAX_PATH];
  if (!p_exists(path)) return;
  SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
  if (DeleteFileA(path)) { slog("deleted %s", path); return; }
  slog("in use: %s (deleted when Windows restarts)", path);
  if (GetShortPathNameA(path, sp, sizeof(sp)) && wininit_line("NUL", sp)) *reboot = 1;
}
