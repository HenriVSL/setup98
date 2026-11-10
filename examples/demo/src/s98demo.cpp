// s98demo.cpp - Setup98 Demo: a tiny Win32 program to package with Setup98 (examples/demo.ini).
// It shows what an installer has to care about:
//   - program files next to the exe (data\logo.bmp, data\tips.txt): File= lines, replaced by every upgrade;
//   - default settings it only reads (s98demo.ini): a Keep= file, installed only if missing;
//   - data it writes itself (s98demo.sav, a start counter): a UserData= file, never shipped, deleted only when the
//     user answers "Yes" to the uninstall question;
//   - a document type (.greet): s98demo.exe <file.greet> shows that file ([Types] in the manifest).
// Build with VC6: nmake in this folder (writes ..\s98demo.exe). Windows 95/98/NT, GDI only.
#include <windows.h>
#include <string.h>
#include <stdio.h>

#define CLASS_NAME "S98Demo"
#define TEXT_MAX 2048

static char g_dir[MAX_PATH];           // folder of the exe, with a trailing backslash
static char g_title[128] = "Hello from Setup98!";
static char g_text[TEXT_MAX];
static char g_tip[256];
static char g_footer[400];
static COLORREF g_back = RGB(192, 192, 192);
static HBITMAP g_logo = 0;
static HFONT g_big = 0;

static void path_of(char* out, const char* name)
{
  lstrcpyn(out, g_dir, MAX_PATH);
  if (lstrlen(out) + lstrlen(name) < MAX_PATH) lstrcat(out, name);
}

// reads up to cap-1 bytes of a text file; returns 0 if it can't be opened
static int read_text(const char* path, char* buf, int cap)
{
  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  DWORD got = 0;
  if (h == INVALID_HANDLE_VALUE) return 0;
  if (!ReadFile(h, buf, (DWORD)(cap - 1), &got, NULL)) got = 0;
  CloseHandle(h);
  buf[got] = 0;
  return 1;
}

// line n (0-based, wrapping) of a text buffer, without its line break
static void pick_line(const char* text, int n, char* out, int cap)
{
  const char* p;
  int lines = 0, i;
  out[0] = 0;
  for (p = text; *p; p++) if (*p == '\n') lines++;
  if (p > text && p[-1] != '\n') lines++;
  if (lines == 0) return;
  n %= lines;
  p = text;
  for (i = 0; i < n; i++) p = strchr(p, '\n') + 1;
  for (i = 0; i < cap - 1 && p[i] && p[i] != '\r' && p[i] != '\n'; i++) out[i] = p[i];
  out[i] = 0;
}

static void load_state(const char* arg)
{
  char ini[MAX_PATH], sav[MAX_PATH], path[MAX_PATH], buf[64], tips[4096];
  int r = 192, g = 192, b = 192, starts;
  char* p;

  GetModuleFileName(NULL, g_dir, MAX_PATH);
  p = strrchr(g_dir, '\\');
  if (p) p[1] = 0;
  path_of(ini, "s98demo.ini");
  path_of(sav, "s98demo.sav");

  // settings (Keep file): read only
  GetPrivateProfileString("Demo", "Title", g_title, g_title, sizeof(g_title), ini);
  GetPrivateProfileString("Demo", "Greeting", "This program was installed by a Setup98 setup.", g_text,
                          sizeof(g_text), ini);
  GetPrivateProfileString("Demo", "Background", "192,192,192", buf, sizeof(buf), ini);
  if (sscanf(buf, "%d,%d,%d", &r, &g, &b) == 3) g_back = RGB(r & 255, g & 255, b & 255);

  // user data: the start counter
  starts = (int)GetPrivateProfileInt("Stats", "Starts", 0, sav) + 1;
  wsprintf(buf, "%d", starts);
  WritePrivateProfileString("Stats", "Starts", buf, sav);
  WritePrivateProfileString(NULL, NULL, NULL, sav);           // flush the profile cache

  // a .greet document from the command line replaces the greeting
  if (arg[0]) {
    if (read_text(arg, g_text, sizeof(g_text))) {
      p = strrchr(arg, '\\');
      wsprintf(g_title, "%.100s", p ? p + 1 : arg);
    } else {
      wsprintf(g_text, "Cannot open %.200s", arg);
    }
  }

  path_of(path, "data\\tips.txt");
  if (read_text(path, tips, sizeof(tips))) pick_line(tips, starts - 1, g_tip, sizeof(g_tip));
  else lstrcpy(g_tip, "(data\\tips.txt is missing)");

  wsprintf(g_footer, "Started %d time%s (counted in s98demo.sav). Settings: s98demo.ini.\nFolder: %.200s", starts,
           starts == 1 ? "" : "s", g_dir);

  path_of(path, "data\\logo.bmp");
  g_logo = (HBITMAP)LoadImage(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
}

static void paint(HWND hwnd, HDC dc)
{
  RECT rc, r;
  HBRUSH br = CreateSolidBrush(g_back);
  HFONT old;
  GetClientRect(hwnd, &rc);
  FillRect(dc, &rc, br);
  DeleteObject(br);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(0, 0, 0));

  if (g_logo) {
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ prev = SelectObject(mem, g_logo);
    BitBlt(dc, 16, 16, 96, 96, mem, 0, 0, SRCCOPY);
    SelectObject(mem, prev);
    DeleteDC(mem);
  } else {
    SetRect(&r, 16, 16, 112, 112);
    DrawText(dc, "(data\\logo.bmp is missing)", -1, &r, DT_WORDBREAK);
  }

  old = (HFONT)SelectObject(dc, g_big);
  SetRect(&r, 128, 16, rc.right - 16, 48);
  DrawText(dc, g_title, -1, &r, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  SelectObject(dc, old);

  SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
  SetRect(&r, 128, 52, rc.right - 16, 128);
  DrawText(dc, g_text, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
  SetRect(&r, 16, 132, rc.right - 16, 170);
  DrawText(dc, "Tip:", -1, &r, DT_SINGLELINE | DT_NOPREFIX);
  SetRect(&r, 48, 132, rc.right - 16, 170);
  DrawText(dc, g_tip, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
  SetTextColor(dc, RGB(64, 64, 64));
  SetRect(&r, 16, rc.bottom - 40, rc.right - 16, rc.bottom - 8);
  DrawText(dc, g_footer, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_PATH_ELLIPSIS);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    paint(hwnd, dc);
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_ERASEBKGND:
    return 1;
  case WM_SIZE:
    InvalidateRect(hwnd, NULL, FALSE);
    return 0;
  case WM_KEYDOWN:
    if (wp == VK_ESCAPE) DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
  WNDCLASS wc;
  HWND hwnd;
  MSG m;
  char arg[MAX_PATH];
  int i = 0;
  (void)prev;

  // the command line is one optional file name, with or without quotes
  while (*cmd == ' ') cmd++;
  if (*cmd == '"') {
    cmd++;
    while (*cmd && *cmd != '"' && i < MAX_PATH - 1) arg[i++] = *cmd++;
  } else {
    while (*cmd && i < MAX_PATH - 1) arg[i++] = *cmd++;
    while (i > 0 && arg[i - 1] == ' ') i--;
  }
  arg[i] = 0;
  load_state(arg);

  g_big = CreateFont(-20, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");

  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
  wc.lpszClassName = CLASS_NAME;
  RegisterClass(&wc);

  hwnd = CreateWindow(CLASS_NAME, "Setup98 Demo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 280, NULL,
                      NULL, inst, NULL);
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);

  while (GetMessage(&m, NULL, 0, 0)) {
    TranslateMessage(&m);
    DispatchMessage(&m);
  }
  if (g_logo) DeleteObject(g_logo);
  if (g_big) DeleteObject(g_big);
  return (int)m.wParam;
}
