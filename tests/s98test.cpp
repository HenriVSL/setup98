// s98test.cpp - Setup98 VM test helper (console, not shipped), adapted from Pica's shtest. Run through agent98:
//   s98test md5 <dir>                     MD5, size and relative path of every file below dir (sorted)
//   s98test lnk <file.lnk>                target, arguments, working folder, description, icon of a shortcut
//   s98test exec <verb|-> <file> [wait]   ShellExecuteEx(verb, file)
//   s98test fakewin <class> <secs>        shows a window of that class for secs (pretends the program runs)
//   s98test lock <file> <secs>            holds a file open without sharing for secs (pretends it is in use)
//   s98test findwin <class>               exit 0 if a window of that class exists
//   s98test closewin <class> [secs]       WM_CLOSE to every window of that class, waits until none is left
//   s98test assoc <.ext>...               HKCR\.ext, its ProgID's open/edit commands, HKCR\*\shell\<id> (env S98ID)
//   s98test key <HKCR|HKLM> <path>        exit 0 if the key exists
//   s98test val <HKCR|HKLM> <path> [name] prints a value
//   s98test verbs <.ext>                  every verb of the extension's ProgID with its command, and ShellNew
//   s98test typename <file>               SHGetFileInfo type name (Explorer's Type column)
//   s98test wininit                       prints <windows>\WININIT.INI
//   s98test append <file> <text>          appends a line (CRLF)
//   s98test ms                            prints GetTickCount (for timing from batch files)
// Exit code 0 = as expected, 1 = not.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- MD5 (RFC 1321) ----

struct Md5 {
  unsigned int a, b, c, d, len_lo, len_hi;
  unsigned char buf[64];
  int n;
};

#define ROL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

static void md5_block(Md5* m, const unsigned char* p)
{
  static const unsigned int K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8,
    0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87,
    0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
    0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
    0xeb86d391 };
  static const int R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5,
                             9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10,
                             15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
  unsigned int w[16], a = m->a, b = m->b, c = m->c, d = m->d, f, t;
  int i, g;
  for (i = 0; i < 16; i++)
    w[i] = (unsigned int)p[i * 4] | ((unsigned int)p[i * 4 + 1] << 8) | ((unsigned int)p[i * 4 + 2] << 16) |
           ((unsigned int)p[i * 4 + 3] << 24);
  for (i = 0; i < 64; i++) {
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
    else { f = c ^ (b | ~d); g = (7 * i) % 16; }
    t = d;
    d = c;
    c = b;
    b = b + ROL(a + f + K[i] + w[g], R[i]);
    a = t;
  }
  m->a += a; m->b += b; m->c += c; m->d += d;
}

static void md5_init(Md5* m)
{
  m->a = 0x67452301; m->b = 0xefcdab89; m->c = 0x98badcfe; m->d = 0x10325476;
  m->len_lo = m->len_hi = 0;
  m->n = 0;
}

static void md5_add(Md5* m, const unsigned char* p, unsigned int n)
{
  unsigned int old = m->len_lo;
  m->len_lo += n << 3;
  if (m->len_lo < old) m->len_hi++;
  m->len_hi += n >> 29;
  while (n) {
    int k = 64 - m->n;
    if ((unsigned int)k > n) k = (int)n;
    memcpy(m->buf + m->n, p, (size_t)k);
    m->n += k; p += k; n -= (unsigned int)k;
    if (m->n == 64) { md5_block(m, m->buf); m->n = 0; }
  }
}

static void md5_end(Md5* m, char* hex)
{
  unsigned char pad = 0x80, z = 0, len[8], out[16];
  unsigned int lo = m->len_lo, hi = m->len_hi, v[4];
  int i;
  for (i = 0; i < 4; i++) { len[i] = (unsigned char)(lo >> (8 * i)); len[4 + i] = (unsigned char)(hi >> (8 * i)); }
  md5_add(m, &pad, 1);
  while (m->n != 56) md5_add(m, &z, 1);
  md5_add(m, len, 8);
  v[0] = m->a; v[1] = m->b; v[2] = m->c; v[3] = m->d;
  for (i = 0; i < 16; i++) out[i] = (unsigned char)(v[i / 4] >> (8 * (i % 4)));
  for (i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", out[i]);
}

// ---- md5 listing ----

static char g_names[4000][300];
static int g_nnames;

static void walk(const char* base, const char* rel)
{
  WIN32_FIND_DATAA fd;
  HANDLE h;
  char spec[600], sub[600];
  sprintf(spec, "%s%s%s\\*.*", base, rel[0] ? "\\" : "", rel);
  h = FindFirstFileA(spec, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
    if (rel[0]) sprintf(sub, "%s\\%s", rel, fd.cFileName);
    else strcpy(sub, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(base, sub);
    else if (g_nnames < 4000) strcpy(g_names[g_nnames++], sub);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

static int cmp_name(const void* a, const void* b)
{
  return lstrcmpiA((const char*)a, (const char*)b);
}

static int do_md5(const char* dir)
{
  int i;
  static unsigned char buf[65536];
  walk(dir, "");
  qsort(g_names, (size_t)g_nnames, sizeof(g_names[0]), cmp_name);
  for (i = 0; i < g_nnames; i++) {
    char path[700], hex[40];
    FILE* f;
    Md5 m;
    unsigned int n, total = 0;
    sprintf(path, "%s\\%s", dir, g_names[i]);
    f = fopen(path, "rb");
    if (!f) { printf("unreadable %s\n", g_names[i]); continue; }
    md5_init(&m);
    while ((n = (unsigned int)fread(buf, 1, sizeof(buf), f)) > 0) { md5_add(&m, buf, n); total += n; }
    fclose(f);
    md5_end(&m, hex);
    printf("%s %u %s\n", hex, total, g_names[i]);
  }
  printf("%d files\n", g_nnames);
  return 0;
}

// ---- shortcuts ----

static const GUID G_CLSID_ShellLink = { 0x00021401, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID G_IID_IShellLinkA = { 0x000214EE, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID G_IID_IPersistFile = { 0x0000010B, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static int do_lnk(const char* path)
{
  IShellLinkA* sl = 0;
  IPersistFile* pf = 0;
  WCHAR w[MAX_PATH];
  char s[MAX_PATH];
  int idx = 0, ok = 0;
  CoInitialize(0);
  if (SUCCEEDED(CoCreateInstance(G_CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER, G_IID_IShellLinkA, (void**)&sl)) && sl &&
      SUCCEEDED(sl->QueryInterface(G_IID_IPersistFile, (void**)&pf)) && pf) {
    MultiByteToWideChar(CP_ACP, 0, path, -1, w, MAX_PATH);
    if (SUCCEEDED(pf->Load(w, STGM_READ))) {
      WIN32_FIND_DATAA fd;
      sl->GetPath(s, sizeof(s), &fd, 0); printf("target:  %s\n", s);
      sl->GetArguments(s, sizeof(s)); printf("args:    %s\n", s);
      sl->GetWorkingDirectory(s, sizeof(s)); printf("workdir: %s\n", s);
      sl->GetDescription(s, sizeof(s)); printf("desc:    %s\n", s);
      sl->GetIconLocation(s, sizeof(s), &idx); printf("icon:    %s,%d\n", s, idx);
      ok = 1;
    } else printf("cannot load %s\n", path);
  }
  if (pf) pf->Release();
  if (sl) sl->Release();
  CoUninitialize();
  return ok ? 0 : 1;
}

// ---- windows ----

static LRESULT CALLBACK fake_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(w, m, wp, lp);
}

static int do_fakewin(const char* cls, int secs)
{
  WNDCLASSA wc;
  HWND w;
  MSG m;
  DWORD end = GetTickCount() + (DWORD)secs * 1000;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = fake_proc;
  wc.hInstance = GetModuleHandleA(0);
  wc.lpszClassName = cls;
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassA(&wc);
  w = CreateWindowA(cls, "s98test fake window", WS_OVERLAPPEDWINDOW, 10, 10, 200, 80, 0, 0, wc.hInstance, 0);
  ShowWindow(w, SW_SHOWNOACTIVATE);
  printf("window %s for %d s\n", cls, secs);
  fflush(stdout);
  while (GetTickCount() < end && IsWindow(w)) {
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
    Sleep(50);
  }
  if (IsWindow(w)) DestroyWindow(w);
  return 0;
}

static void reg_print(HKEY root, const char* key)
{
  char val[600];
  LONG n = sizeof(val);
  if (RegQueryValueA(root, key, val, &n) == ERROR_SUCCESS) printf("  %s = \"%s\"\n", key, val);
  else printf("  %s: (none)\n", key);
}

static HKEY root_of(const char* s)
{
  return !lstrcmpiA(s, "HKCR") ? HKEY_CLASSES_ROOT : !lstrcmpiA(s, "HKCU") ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

int main(int argc, char** argv)
{
  int i;
  if (argc < 2) { printf("usage: see s98test.cpp\n"); return 1; }
  if (!lstrcmpiA(argv[1], "md5") && argc >= 3) return do_md5(argv[2]);
  if (!lstrcmpiA(argv[1], "lnk") && argc >= 3) return do_lnk(argv[2]);
  if (!lstrcmpiA(argv[1], "ms")) { printf("%lu\n", GetTickCount()); return 0; }
  if (!lstrcmpiA(argv[1], "exec") && argc >= 4) {
    SHELLEXECUTEINFOA sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = lstrcmpA(argv[2], "-") ? argv[2] : 0;
    sei.lpFile = argv[3];
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei)) { printf("exec failed: error %lu\n", GetLastError()); return 1; }
    printf("exec ok (%s %s)\n", argv[2], argv[3]);
    if (sei.hProcess) {
      if (argc >= 5 && !lstrcmpiA(argv[4], "wait")) {
        DWORD r = WaitForSingleObject(sei.hProcess, 60000), code = 0;
        GetExitCodeProcess(sei.hProcess, &code);
        printf("process %s, exit code %lu\n", r == WAIT_OBJECT_0 ? "ended" : "still running after 60 s", code);
      }
      CloseHandle(sei.hProcess);
    }
    return 0;
  }
  if (!lstrcmpiA(argv[1], "fakewin") && argc >= 4) return do_fakewin(argv[2], atoi(argv[3]));
  if (!lstrcmpiA(argv[1], "lock") && argc >= 4) {
    HANDLE h = CreateFileA(argv[2], GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot open %s (%lu)\n", argv[2], GetLastError()); return 1; }
    printf("locked %s for %s s\n", argv[2], argv[3]);
    fflush(stdout);
    Sleep((DWORD)atoi(argv[3]) * 1000);
    CloseHandle(h);
    return 0;
  }
  if (!lstrcmpiA(argv[1], "findwin") && argc >= 3) {
    HWND w = FindWindowA(argv[2], 0);
    printf("%s\n", w ? "found" : "none");
    return w ? 0 : 1;
  }
  if (!lstrcmpiA(argv[1], "closewin") && argc >= 3) {
    int secs = argc >= 4 ? atoi(argv[3]) : 20;
    HWND w;
    while ((w = FindWindowA(argv[2], 0)) != 0 && secs-- > 0) { PostMessageA(w, WM_CLOSE, 0, 0); Sleep(1000); }
    printf("%s\n", FindWindowA(argv[2], 0) ? "still open" : "closed");
    return FindWindowA(argv[2], 0) ? 1 : 0;
  }
  if (!lstrcmpiA(argv[1], "assoc") && argc >= 3) {
    char key[300], cur[300];
    const char* id = getenv("S98ID");
    for (i = 2; i < argc; i++) {
      LONG n = sizeof(cur);
      printf("%s\n", argv[i]);
      reg_print(HKEY_CLASSES_ROOT, argv[i]);
      if (RegQueryValueA(HKEY_CLASSES_ROOT, argv[i], cur, &n) == ERROR_SUCCESS && cur[0]) {
        sprintf(key, "%s\\shell\\open\\command", cur);
        reg_print(HKEY_CLASSES_ROOT, key);
        sprintf(key, "%s\\shell\\edit\\command", cur);
        reg_print(HKEY_CLASSES_ROOT, key);
      }
    }
    sprintf(key, "*\\shell\\%s\\command", id ? id : "Pica");
    reg_print(HKEY_CLASSES_ROOT, key);
    return 0;
  }
  if (!lstrcmpiA(argv[1], "key") && argc >= 4) {
    HKEY h;
    if (RegOpenKeyExA(root_of(argv[2]), argv[3], 0, KEY_READ, &h) == ERROR_SUCCESS) {
      RegCloseKey(h);
      printf("exists\n");
      return 0;
    }
    printf("absent\n");
    return 1;
  }
  if (!lstrcmpiA(argv[1], "val") && argc >= 4) {
    HKEY h;
    char v[2048];
    DWORD n = sizeof(v), type = 0;
    if (RegOpenKeyExA(root_of(argv[2]), argv[3], 0, KEY_READ, &h) != ERROR_SUCCESS) { printf("no key\n"); return 1; }
    if (RegQueryValueExA(h, argc >= 5 ? argv[4] : 0, 0, &type, (BYTE*)v, &n) != ERROR_SUCCESS) {
      RegCloseKey(h);
      printf("no value\n");
      return 1;
    }
    RegCloseKey(h);
    if (type == REG_DWORD) printf("dword %lu\n", *(DWORD*)v);
    else printf("\"%s\"\n", v);
    return 0;
  }
  if (!lstrcmpiA(argv[1], "verbs") && argc >= 3) {
    char pid[300], key[400], name[260], cmd[600];
    LONG n = sizeof(pid);
    HKEY h;
    DWORD k, nl;
    if (RegQueryValueA(HKEY_CLASSES_ROOT, argv[2], pid, &n) != ERROR_SUCCESS || !pid[0]) { printf("no ProgID\n"); return 1; }
    printf("%s -> %s\n", argv[2], pid);
    sprintf(key, "%s\\shell", pid);
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
      for (k = 0;; k++) {
        char ck[700];
        LONG cn = sizeof(cmd);
        nl = sizeof(name);
        if (RegEnumKeyExA(h, k, name, &nl, 0, 0, 0, 0) != ERROR_SUCCESS) break;
        sprintf(ck, "%s\\%s\\command", key, name);
        if (RegQueryValueA(HKEY_CLASSES_ROOT, ck, cmd, &cn) != ERROR_SUCCESS) strcpy(cmd, "(no command)");
        printf("  %s: %s\n", name, cmd);
      }
      RegCloseKey(h);
    }
    sprintf(key, "%s\\ShellNew", argv[2]);
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &h) == ERROR_SUCCESS) { printf("  ShellNew present\n"); RegCloseKey(h); }
    return 0;
  }
  if (!lstrcmpiA(argv[1], "typename") && argc >= 3) {
    SHFILEINFOA fi;
    memset(&fi, 0, sizeof(fi));
    SHGetFileInfoA(argv[2], 0, &fi, sizeof(fi), SHGFI_TYPENAME);
    printf("%s: %s\n", argv[2], fi.szTypeName);
    return 0;
  }
  if (!lstrcmpiA(argv[1], "wininit")) {
    char path[MAX_PATH], line[600];
    FILE* f;
    GetWindowsDirectoryA(path, sizeof(path));
    strcat(path, "\\WININIT.INI");
    f = fopen(path, "r");
    if (!f) { printf("no WININIT.INI\n"); return 0; }
    while (fgets(line, sizeof(line), f)) printf("%s", line);
    fclose(f);
    return 0;
  }
  if (!lstrcmpiA(argv[1], "append") && argc >= 4) {
    FILE* f = fopen(argv[2], "ab");
    if (!f) return 1;
    fprintf(f, "%s\r\n", argv[3]);
    fclose(f);
    return 0;
  }
  printf("unknown command\n");
  return 1;
}
