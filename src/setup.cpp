// setup.cpp - Setup98 stub: command line, install wizard (PropertySheet, PSH_WIZARD), silent install, uninstall
// (asks, then inst.cpp does the work). Generalised from Pica's setup: the product comes from the manifest in the
// payload. The same exe is the installer (payload with files), the uninstaller (manifest only) and, without a
// payload, the plain stub that pack98 copies. Switches: README.md.
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string.h>
#include "s98.h"
#include "ids.h"

enum { M_FRESH, M_UPGRADE, M_REPAIR, M_DOWNGRADE };
enum { WM_APP_START = WM_APP + 1 };
enum { ID_SHEET_NEXT = 0x3024 };

Manifest g_m;
static HINSTANCE g_inst;
static Opts g_o;
static InstallInfo g_ii;
static char g_caption[160];
static int g_mode = M_FRESH;
static char g_oldver[48];             // version replaced by this run (upgrade / repair / downgrade)
static int g_rc = RC_FAIL;
static int g_ran;                     // inst_run was called
static int g_cancelled;
static char g_err[700];
static HFONT g_bold;
static int g_do_run, g_do_reboot, g_do_readme;
static int g_last_page;               // dialog id of the page whose Next says "Install"
static u32 g_need_kb;

// command-line values applied after the defaults of an existing install are known
static char g_arg_types[600];
static int g_arg_start = -1, g_arg_desk = -1, g_arg_ctx = -1;
static char g_args_raw[1200];         // everything after the program name (passed on to the uninstall copy)
static char g_arg_dir[MAX_PATH];      // /D=

// ---- small helpers ----

static int msg(HWND owner, const char* text, UINT flags)
{
  return MessageBoxA(owner, text, g_caption, flags | MB_SETFOREGROUND);
}

static void launch_app(const char* dir, int readme)
{
  char exe[MAX_PATH], cmd[MAX_PATH * 2];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  if (readme) {
    p_join(dir, g_m.readme, exe, sizeof(exe));
    if ((int)ShellExecuteA(0, "open", exe, 0, dir, SW_SHOWNORMAL) <= 32) slog("error: cannot open %s", exe);
    else slog("opened %s", exe);
    return;
  }
  p_join(dir, g_m.mainexe, exe, sizeof(exe));
  s_copy(cmd, sizeof(cmd), "\"");
  s_cat(cmd, sizeof(cmd), exe);
  s_cat(cmd, sizeof(cmd), "\"");
  if (g_m.run_args[0]) { s_cat(cmd, sizeof(cmd), " "); s_cat(cmd, sizeof(cmd), g_m.run_args); }
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  if (CreateProcessA(exe, cmd, 0, 0, FALSE, 0, 0, dir, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    slog("started %s", cmd);
  } else {
    slog("error: cannot start %s (%lu)", exe, GetLastError());
  }
}

static void usage_text(char* out, int cap)
{
  wsprintfA(out, "%s setup switches:\n\n"
                 "/S\tsilent install with the defaults (or the choices of the installed copy)\n"
                 "/D=<folder>\tinstall folder (last switch, no quotes needed)\n"
                 "/STARTMENU=0|1  /DESKTOP=0|1\tshortcuts\n", g_m.name);
  if (g_m.has_context) s_cat(out, cap, "/CONTEXT=0|1\t\"Open with\" in the right-click menu of all files\n");
  if (g_m.has_types) s_cat(out, cap, "/TYPES=<list>\tfile types: default, none, all, .txt,.c or +.bat,-.ini\n");
  s_cat(out, cap, "/RUN\tstart the program when done\n"
                  "/FORCE\tallow a downgrade in silent mode\n"
                  "/LOG=<file>\tlog file\n"
                  "/UNINSTALL [/S] [/PURGE]\tremove it (/PURGE also deletes settings and saved data)\n\n"
                  "Exit codes: 0 ok, 1 error or cancelled, 2 program running, 3 restart needed, "
                  "4 newer version installed, 5 not enough disk space.");
}

static void kb_text(u32 kb, char* out)
{
  if (kb == 0xFFFFFFFFu) s_copy(out, 32, "more than 4 TB");
  else if (kb >= 10240) wsprintfA(out, "%lu MB", (kb + 512) / 1024);
  else if (kb >= 1024) wsprintfA(out, "%lu.%lu MB", kb / 1024, (kb % 1024) * 10 / 1024);
  else wsprintfA(out, "%lu KB", kb);
}

// ---- command line ----

static int parse_args(char* bad, int cap)
{
  const char* cl = GetCommandLineA();
  char tok[MAX_PATH * 2];
  bad[0] = 0;
  if (*cl == '"') {
    cl++;
    while (*cl && *cl != '"') cl++;
    if (*cl) cl++;
  } else {
    while (*cl && *cl != ' ' && *cl != '\t') cl++;
  }
  while (*cl == ' ' || *cl == '\t') cl++;
  s_copy(g_args_raw, sizeof(g_args_raw), cl);
  for (;;) {
    int n = 0, q = 0;
    while (*cl == ' ' || *cl == '\t') cl++;
    if (!*cl) break;
    if (starts_i(cl, "/D=") || starts_i(cl, "\"/D=")) {        // NSIS style: the rest of the line is the folder
      const char* d = cl + (*cl == '"' ? 4 : 3);
      if (!p_normalize_dir(d, g_arg_dir, sizeof(g_arg_dir))) { s_copy(bad, cap, cl); return 0; }
      g_o.dir_given = 1;
      break;
    }
    while (*cl && (q || (*cl != ' ' && *cl != '\t'))) {
      if (*cl == '"') q = !q;
      else if (n < (int)sizeof(tok) - 1) tok[n++] = *cl;
      cl++;
    }
    tok[n] = 0;
    if (tok[0] == '-') tok[0] = '/';
    if (!lstrcmpiA(tok, "/S")) g_o.silent = 1;
    else if (!lstrcmpiA(tok, "/UNINSTALL")) g_o.uninstall = 1;
    else if (!lstrcmpiA(tok, "/PURGE")) g_o.purge = 1;
    else if (!lstrcmpiA(tok, "/FORCE")) g_o.force = 1;
    else if (!lstrcmpiA(tok, "/RUN")) g_o.run_after = 1;
    else if (!lstrcmpiA(tok, "/_LOCKTEST")) g_o.locktest = 1;
    else if (!lstrcmpiA(tok, "/_FIXNAMES")) g_o.fixnames = 1;
    else if (starts_i(tok, "/LOG=")) s_copy(g_o.log, sizeof(g_o.log), tok + 5);
    else if (starts_i(tok, "/TYPES=")) s_copy(g_arg_types, sizeof(g_arg_types), tok + 7);
    else if (starts_i(tok, "/STARTMENU=")) g_arg_start = tok[11] == '1';
    else if (starts_i(tok, "/DESKTOP=")) g_arg_desk = tok[9] == '1';
    else if (starts_i(tok, "/CONTEXT=")) g_arg_ctx = tok[9] == '1';
    else if (starts_i(tok, "/_CHILD=")) {
      const char* p = tok + 8;
      while (*p >= '0' && *p <= '9') g_o.child_pid = g_o.child_pid * 10 + (DWORD)(*p++ - '0');
    } else { s_copy(bad, cap, tok); return 0; }
  }
  return 1;
}

static int find_ext(const char* e)
{
  int i;
  char want[24];
  if (*e != '.') { s_copy(want, sizeof(want), "."); s_cat(want, sizeof(want), e); }
  else s_copy(want, sizeof(want), e);
  for (i = 0; i < g_m.next; i++) if (!lstrcmpiA(g_m.exts[i].ext, want)) return i;
  return -1;
}

// /TYPES=default|none|all|<list>; list items with + or - change the stored/default set, plain items replace it
static int apply_types(const char* spec, char* bad, int cap)
{
  char item[32];
  const char* s = spec;
  int i, first = 1;
  if (!spec[0]) return 1;
  if (!lstrcmpiA(spec, "default")) { opts_default_types(&g_o); return 1; }
  if (!lstrcmpiA(spec, "none") || !lstrcmpiA(spec, "all")) {
    for (i = 0; i < g_m.next; i++) g_o.types[i] = spec[0] == 'a' || spec[0] == 'A';
    return 1;
  }
  while (*s) {
    int n = 0, idx, sign = 0;
    while (*s == ',' || *s == ';') s++;
    if (!*s) break;
    while (*s && *s != ',' && *s != ';' && n < (int)sizeof(item) - 1) item[n++] = *s++;
    item[n] = 0;
    if (item[0] == '+') sign = 1;
    else if (item[0] == '-') sign = -1;
    if (first && !sign) for (i = 0; i < g_m.next; i++) g_o.types[i] = 0;
    first = 0;
    idx = find_ext(sign ? item + 1 : item);
    if (idx < 0) { s_copy(bad, cap, item); return 0; }
    g_o.types[idx] = sign >= 0 ? 1 : 0;
  }
  return 1;
}

static int compute_mode(const char* dir, char* oldver)
{
  DWORD ms, ls;
  char exe[MAX_PATH];
  int c;
  oldver[0] = 0;
  if (g_ii.installed && g_ii.have_ver && p_same(dir, g_ii.dir)) { ms = g_ii.ms; ls = g_ii.ls; }
  else {
    p_join(dir, g_m.mainexe, exe, sizeof(exe));
    if (!file_version(exe, &ms, &ls)) return g_ii.installed && p_same(dir, g_ii.dir) ? M_REPAIR : M_FRESH;
  }
  if (g_ii.installed && g_ii.have_ver && p_same(dir, g_ii.dir)) s_copy(oldver, 48, g_ii.ver);   // its shown version
  else ver_format(ms, ls, oldver);
  c = ver_compare(ms, ls, g_m.ver_ms, g_m.ver_ls);
  return c < 0 ? M_UPGRADE : c == 0 ? M_REPAIR : M_DOWNGRADE;
}

static void running_text(char* out)
{
  wsprintfA(out, "%s is running. Please close it, then click Retry.\n\nCancel stops without changing anything.",
            g_m.name);
}

// ---- uninstall ----

static void handshake_names(DWORD pid, char* rcname, char* evname)
{
  wsprintfA(rcname, "Setup98_%s_Rc%lu", g_m.id, pid);
  wsprintfA(evname, "Setup98_%s_Done%lu", g_m.id, pid);
}

static int uninstall_spawn_child()
{
  char tmpdir[MAX_PATH], tmp[MAX_PATH], self[MAX_PATH], cmd[MAX_PATH + 1400], name[80], rcname[120], evname[120];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  HANDLE map, ev, wait[2];
  DWORD* rcp;
  int rc;
  GetTempPathA(sizeof(tmpdir), tmpdir);
  m_temp_name(&g_m, name);
  s_cat(name, sizeof(name), ".EXE");
  p_join(tmpdir, name, tmp, sizeof(tmp));
  GetModuleFileNameA(0, self, sizeof(self));
  SetFileAttributesA(tmp, FILE_ATTRIBUTE_NORMAL);
  if (!CopyFileA(self, tmp, FALSE)) { slog("cannot copy the uninstaller to %s", tmp); return -1; }
  handshake_names(GetCurrentProcessId(), rcname, evname);
  map = CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 4, rcname);
  rcp = map ? (DWORD*)MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 4) : 0;
  ev = CreateEventA(0, TRUE, FALSE, evname);
  if (!rcp || !ev) { slog("cannot create the uninstall handshake"); return -1; }
  *rcp = RC_FAIL;
  wsprintfA(cmd, "\"%s\" /UNINSTALL /_CHILD=%lu ", tmp, GetCurrentProcessId());
  s_cat(cmd, sizeof(cmd), g_args_raw);
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  slog("running the uninstaller from %s", tmp);
  log_close();
  if (!CreateProcessA(tmp, cmd, 0, 0, FALSE, 0, 0, tmpdir, &si, &pi)) {
    log_open(g_o.log, 0);
    slog("cannot start %s (%lu)", tmp, GetLastError());
    return -1;
  }
  CloseHandle(pi.hThread);
  wait[0] = ev;
  wait[1] = pi.hProcess;
  WaitForMultipleObjects(2, wait, FALSE, INFINITE);
  rc = (int)*rcp;
  CloseHandle(pi.hProcess);
  UnmapViewOfFile(rcp);
  CloseHandle(map);
  CloseHandle(ev);
  return rc;
}

static void uninstall_report(int rc)
{
  char rcname[120], evname[120];
  HANDLE map, ev;
  DWORD* rcp;
  handshake_names(g_o.child_pid, rcname, evname);
  map = OpenFileMappingA(FILE_MAP_WRITE, FALSE, rcname);
  rcp = map ? (DWORD*)MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 4) : 0;
  if (rcp) { *rcp = (DWORD)rc; UnmapViewOfFile(rcp); }
  if (map) CloseHandle(map);
  ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, evname);
  if (ev) { SetEvent(ev); CloseHandle(ev); }
}

static int uninstall_main()
{
  char dir[MAX_PATH], mdir[MAX_PATH], text[1200], path[MAX_PATH], self[MAX_PATH], recname[64];
  int purge = g_o.purge, kept = 0, rc, child = g_o.child_pid != 0;
  wsprintfA(g_caption, "%s Uninstall", g_m.name);
  detect_install(&g_ii);
  p_module_dir(mdir, sizeof(mdir));
  m_record_name(&g_m, recname);
  if (g_ii.installed) s_copy(dir, sizeof(dir), g_ii.dir);
  else {
    p_join(mdir, recname, path, sizeof(path));
    if (!child && p_exists(path)) s_copy(dir, sizeof(dir), mdir);     // registry lost: clean the files only
    else {
      slog("%s is not installed", g_m.name);
      wsprintfA(text, "%s is not installed on this computer.", g_m.name);
      if (!g_o.silent) msg(0, text, MB_OK | MB_ICONINFORMATION);
      if (child) uninstall_report(RC_FAIL);
      return RC_FAIL;
    }
  }
  slog("uninstall: installed %d, folder %s, version %s, child of %08lX", g_ii.installed, dir, g_ii.ver, g_o.child_pid);

  // the installed uninstaller can't delete itself: a temp copy does the work while this one waits
  if (!child && p_same(mdir, dir)) {
    rc = uninstall_spawn_child();
    if (rc >= 0) return rc;
    slog("continuing in place (the uninstaller is deleted when Windows restarts)");
  }

  if (!g_o.silent) {
    wsprintfA(text, "Remove %s %s from %s?", g_m.name, g_ii.installed ? g_ii.ver : "", dir);
    if (msg(0, text, MB_YESNO | MB_ICONQUESTION) != IDYES) {
      slog("cancelled");
      if (child) uninstall_report(RC_FAIL);
      return RC_FAIL;
    }
  }
  while (app_running(dir)) {
    slog("%s is running", g_m.name);
    running_text(text);
    if (g_o.silent || msg(0, text, MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY) {
      if (child) uninstall_report(g_o.silent ? RC_RUNNING : RC_FAIL);
      return g_o.silent ? RC_RUNNING : RC_FAIL;
    }
  }
  if (!g_o.silent && userdata_exists(dir)) {
    wsprintfA(text, "Also delete %s?\n\nChoose No to keep them: %s finds them again if you reinstall it into the same "
                    "folder.", g_m.data_name, g_m.name);
    purge = msg(0, text, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
  }
  rc = uninst_run(dir, purge, child, &kept);
  kept = !purge && userdata_exists(dir);
  if (!g_o.silent) {
    wsprintfA(text, "%s was removed from your computer.", g_m.name);
    if (kept) {
      s_cat(text, sizeof(text), "\n\nYour settings and saved data were kept in ");
      s_cat(text, sizeof(text), dir);
      s_cat(text, sizeof(text), ".");
    }
    if (rc == RC_REBOOT) s_cat(text, sizeof(text), "\n\nSome files were in use: restart Windows to remove them.");
    msg(0, text, MB_OK | MB_ICONINFORMATION);
  }
  if (child) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, g_o.child_pid);
    uninstall_report(rc);
    if (parent) {
      WaitForSingleObject(parent, 15000);
      CloseHandle(parent);
    }
    if (uninst_finish(dir) == RC_REBOOT) slog("the uninstaller is deleted when Windows restarts");
    GetModuleFileNameA(0, self, sizeof(self));
    wininit_delete_later(self);              // this temp copy goes at the next restart
  }
  return rc;
}

// ---- wizard ----

static void page_init(HWND d)
{
  if (!g_bold) {
    LOGFONTA lf;
    HFONT f = (HFONT)SendMessageA(d, WM_GETFONT, 0, 0);
    if (f && GetObjectA(f, sizeof(lf), &lf)) {
      lf.lfWeight = FW_BOLD;
      g_bold = CreateFontIndirectA(&lf);
    }
  }
  if (g_bold && GetDlgItem(d, IDC_TITLE)) SendDlgItemMessageA(d, IDC_TITLE, WM_SETFONT, (WPARAM)g_bold, 0);
}

static void set_result(HWND d, LONG v)
{
  SetWindowLongA(d, DWL_MSGRESULT, v);
}

static int query_cancel(HWND d)
{
  char text[200];
  wsprintfA(text, "Quit %s Setup? Nothing has been changed.", g_m.name);
  if (msg(GetParent(d), text, MB_YESNO | MB_ICONQUESTION) == IDYES) {
    g_cancelled = 1;
    slog("cancelled in the wizard");
    return 0;
  }
  return 1;
}

static int ask_downgrade(HWND owner, const char* oldver)
{
  char text[300];
  wsprintfA(text, "A newer %s (%s) is installed. Replace it with the older version %s?", g_m.name, oldver, m_shown_version(&g_m));
  return msg(owner, text, MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
}

// sets the Next button (Back/Next or Back/Install) for a page
static void wiz_buttons(HWND d, int id, int back)
{
  PropSheet_SetWizButtons(GetParent(d), (back ? PSWIZB_BACK : 0) | PSWIZB_NEXT);
  SetDlgItemTextA(GetParent(d), ID_SHEET_NEXT, id == g_last_page ? "&Install" : "&Next >");
}

static BOOL CALLBACK welcome_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  char text[700];
  (void)wp;
  switch (m) {
  case WM_INITDIALOG:
    page_init(d);
    if (g_mode == M_UPGRADE) wsprintfA(text, "Upgrade %s %s -> %s", g_m.name, g_oldver, m_shown_version(&g_m));
    else if (g_mode == M_REPAIR) wsprintfA(text, "Reinstall %s %s", g_m.name, m_shown_version(&g_m));
    else if (g_mode == M_DOWNGRADE) wsprintfA(text, "Replace %s %s with %s", g_m.name, g_oldver, m_shown_version(&g_m));
    else wsprintfA(text, "Welcome to %s %s Setup", g_m.name, m_shown_version(&g_m));
    SetDlgItemTextA(d, IDC_TITLE, text);
    if (g_m.description[0]) {
      char* p;
      s_copy(text, sizeof(text), "");
      for (p = g_m.description; *p && lstrlenA(text) < (int)sizeof(text) - 3; p++) {   // \n -> CRLF
        char two[3];
        two[0] = *p; two[1] = 0;
        if (*p == '\n') { two[0] = '\r'; two[1] = '\n'; two[2] = 0; }
        s_cat(text, sizeof(text), two);
      }
      SetDlgItemTextA(d, IDC_W_DESC, text);
    }
    text[0] = 0;
    if (g_mode == M_UPGRADE)
      wsprintfA(text, "%s %s is installed in %s. Setup upgrades it to %s and keeps your settings and saved data.",
                g_m.name, g_oldver, g_ii.dir, m_shown_version(&g_m));
    else if (g_mode == M_REPAIR)
      wsprintfA(text, "%s %s is already installed in %s. Setup copies the program files again (repair) and keeps your "
                      "settings and saved data.", g_m.name, m_shown_version(&g_m), g_ii.dir);
    else if (g_mode == M_DOWNGRADE)
      wsprintfA(text, "A newer %s (%s) is installed in %s. Setup can replace it with the older %s; your settings are "
                      "kept.", g_m.name, g_oldver, g_ii.dir, m_shown_version(&g_m));
    else
      wsprintfA(text, "This wizard installs %s on your computer.", g_m.name);
    SetDlgItemTextA(d, IDC_W_STATUS, text);
    return TRUE;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE: wiz_buttons(d, IDD_WELCOME, 0); set_result(d, 0); return TRUE;
    case PSN_WIZNEXT:
      set_result(d, (g_mode == M_DOWNGRADE && !ask_downgrade(GetParent(d), g_oldver)) ? -1 : 0);
      return TRUE;
    case PSN_QUERYCANCEL: set_result(d, query_cancel(d)); return TRUE;
    }
    break;
  }
  return FALSE;
}

static void license_buttons(HWND d)
{
  int ok = !g_m.license_agree || IsDlgButtonChecked(d, IDC_L_AGREE) == BST_CHECKED;
  PropSheet_SetWizButtons(GetParent(d), PSWIZB_BACK | (ok ? PSWIZB_NEXT : 0));
  SetDlgItemTextA(GetParent(d), ID_SHEET_NEXT, IDD_LICENSE == g_last_page ? "&Install" : "&Next >");
}

static BOOL CALLBACK license_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  char text[300];
  switch (m) {
  case WM_INITDIALOG: {
    u32 n = 0;
    u8* t;
    page_init(d);
    t = pay_load_text(g_m.license, &n);
    if (t) {
      SetDlgItemTextA(d, IDC_L_TEXT, (const char*)t);
      HeapFree(GetProcessHeap(), 0, t);
    }
    if (g_m.license_agree)
      wsprintfA(text, "Please read the license agreement of %s. Tick the box below to accept it.", g_m.name);
    else
      wsprintfA(text, "Please read the license of %s.", g_m.name);
    SetDlgItemTextA(d, IDC_L_INTRO, text);
    ShowWindow(GetDlgItem(d, IDC_L_AGREE), g_m.license_agree ? SW_SHOW : SW_HIDE);
    return TRUE;
  }
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_L_AGREE) license_buttons(d);
    break;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE:
      license_buttons(d);
      PostMessageA(GetDlgItem(d, IDC_L_TEXT), EM_SETSEL, 0, 0);
      // focus the accept box (or Next): a multi-line edit with the focus would swallow Enter
      PostMessageA(GetParent(d), WM_NEXTDLGCTL, (WPARAM)(g_m.license_agree ? GetDlgItem(d, IDC_L_AGREE) :
                   GetDlgItem(GetParent(d), ID_SHEET_NEXT)), TRUE);
      set_result(d, 0);
      return TRUE;
    case PSN_WIZNEXT:
      set_result(d, g_m.license_agree && IsDlgButtonChecked(d, IDC_L_AGREE) != BST_CHECKED ? -1 : 0);
      return TRUE;
    case PSN_QUERYCANCEL: set_result(d, query_cancel(d)); return TRUE;
    }
    break;
  }
  return FALSE;
}

static int CALLBACK browse_cb(HWND w, UINT m, LPARAM lp, LPARAM data)
{
  (void)lp;
  if (m == BFFM_INITIALIZED && data) SendMessageA(w, BFFM_SETSELECTION, TRUE, data);
  return 0;
}

// the last folder name of the default folder ("Retro Strike"): Browse appends it
static void folder_leaf(char* out, int cap)
{
  const char* s = strrchr(g_m.folder, '\\');
  s_copy(out, cap, s ? s + 1 : g_m.name);
}

static void browse(HWND d)
{
  BROWSEINFOA bi;
  char cur[MAX_PATH], start[MAX_PATH], disp[MAX_PATH], path[MAX_PATH], leaf[MAX_PATH], title[300];
  LPITEMIDLIST pidl;
  LPMALLOC mal = 0;
  int n, ln;
  char* slash;
  folder_leaf(leaf, sizeof(leaf));
  GetDlgItemTextA(d, IDC_F_DIR, cur, sizeof(cur));
  s_copy(start, sizeof(start), cur);
  while (start[0] && !p_is_dir(start) && (slash = strrchr(start, '\\')) != 0) *slash = 0;   // nearest existing parent
  memset(&bi, 0, sizeof(bi));
  wsprintfA(title, "Choose the folder for %s. Setup adds \\%s unless the folder already ends with it.", g_m.name, leaf);
  bi.hwndOwner = GetParent(d);
  bi.pszDisplayName = disp;
  bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS;
  bi.lpfn = browse_cb;
  bi.lParam = (LPARAM)start;
  pidl = SHBrowseForFolderA(&bi);
  if (!pidl) return;
  if (SHGetPathFromIDListA(pidl, path) && path[0]) {
    n = lstrlenA(path);
    ln = lstrlenA(leaf);
    if (n > 0 && path[n - 1] == '\\') path[--n] = 0;
    if (n < ln + 1 || path[n - ln - 1] != '\\' || lstrcmpiA(path + n - ln, leaf) != 0) {
      s_cat(path, sizeof(path), "\\");
      s_cat(path, sizeof(path), leaf);
    }
    SetDlgItemTextA(d, IDC_F_DIR, path);
  }
  if (SHGetMalloc(&mal) == NOERROR && mal) { mal->Free(pidl); mal->Release(); }
}

static void show_space(HWND d)
{
  char dir[MAX_PATH], text[300], need[32], fr[32];
  u32 kb = 0;
  GetDlgItemTextA(d, IDC_F_DIR, dir, sizeof(dir));
  kb_text(g_need_kb, need);
  if (dir[0] && dir[1] == ':' && space_free_kb(dir, &kb)) {
    kb_text(kb, fr);
    wsprintfA(text, "Space required: %s.  Free on drive %c: %s.", need, dir[0] & ~0x20, fr);
  } else {
    wsprintfA(text, "Space required: %s.", need);
  }
  SetDlgItemTextA(d, IDC_F_SPACE, text);
}

static BOOL CALLBACK folder_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  char text[700], dir[MAX_PATH];
  switch (m) {
  case WM_INITDIALOG: {
    int any = 0, y;
    page_init(d);
    wsprintfA(text, "Install %s in this folder:", g_m.name);
    SetDlgItemTextA(d, IDC_F_LABEL, text);
    SetDlgItemTextA(d, IDC_F_DIR, g_o.dir);
    if (g_m.nlinks > 1) SetDlgItemTextA(d, IDC_F_START, "Create &Start menu shortcuts");
    if (g_m.ndlinks > 1) SetDlgItemTextA(d, IDC_F_DESK, "Create &desktop shortcuts");
    if (g_m.has_context) {
      wsprintfA(text, "Add \"%s\" to the right-click menu of all files", g_m.ctx_text);
      SetDlgItemTextA(d, IDC_F_CTX, text);
    }
    CheckDlgButton(d, IDC_F_START, g_o.startmenu ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(d, IDC_F_DESK, g_o.desktop ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(d, IDC_F_CTX, g_o.context ? BST_CHECKED : BST_UNCHECKED);
    // show only the options this product has, packed from the top of the group box
    {
      static const int ids[3] = { IDC_F_START, IDC_F_DESK, IDC_F_CTX };
      int avail[3], i;
      RECT r;
      avail[0] = g_m.startmenu != OPT_OFF && g_m.nlinks > 0;
      avail[1] = g_m.desktop != OPT_OFF && g_m.ndlinks > 0;
      avail[2] = g_m.has_context;
      y = 100;
      for (i = 0; i < 3; i++) {
        HWND c = GetDlgItem(d, ids[i]);
        if (!avail[i]) { ShowWindow(c, SW_HIDE); continue; }
        r.left = 48; r.top = y; r.right = 298; r.bottom = y + 10;
        MapDialogRect(d, &r);
        SetWindowPos(c, 0, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        y += 13;
        any = 1;
      }
      if (!any) ShowWindow(GetDlgItem(d, IDC_F_GROUP), SW_HIDE);
    }
    if (g_ii.installed) {
      SendDlgItemMessageA(d, IDC_F_DIR, EM_SETREADONLY, TRUE, 0);
      EnableWindow(GetDlgItem(d, IDC_F_BROWSE), FALSE);
      wsprintfA(text, "%s is installed in this folder. To move it, uninstall it first. Only the program files are "
                      "replaced: settings and saved data in this folder stay.", g_m.name);
      SetDlgItemTextA(d, IDC_F_NOTE, text);
    }
    show_space(d);
    return TRUE;
  }
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_F_BROWSE) browse(d);
    else if (LOWORD(wp) == IDC_F_DIR && HIWORD(wp) == EN_CHANGE) show_space(d);
    break;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE: wiz_buttons(d, IDD_FOLDER, 1); set_result(d, 0); return TRUE;
    case PSN_WIZNEXT: {
      u32 kb;
      GetDlgItemTextA(d, IDC_F_DIR, text, sizeof(text));
      if (!p_normalize_dir(text, dir, sizeof(dir))) {
        char ex[MAX_PATH];
        expand_folder(g_m.folder, ex, sizeof(ex));
        wsprintfA(text, "Please enter a full folder path, for example %s.", ex);
        msg(GetParent(d), text, MB_OK | MB_ICONEXCLAMATION);
        set_result(d, -1);
        return TRUE;
      }
      if (space_free_kb(dir, &kb) && kb < g_need_kb) {
        char need[32], fr[32];
        kb_text(g_need_kb, need);
        kb_text(kb, fr);
        wsprintfA(text, "There is not enough free space on drive %c: (%s needed, %s free). Free some space or choose "
                        "another drive.", dir[0] & ~0x20, need, fr);
        msg(GetParent(d), text, MB_OK | MB_ICONEXCLAMATION);
        set_result(d, -1);
        return TRUE;
      }
      if (!g_ii.installed) {
        int mode = compute_mode(dir, g_oldver);
        if (mode == M_DOWNGRADE && !ask_downgrade(GetParent(d), g_oldver)) { set_result(d, -1); return TRUE; }
        g_mode = mode;
      }
      s_copy(g_o.dir, sizeof(g_o.dir), dir);
      SetDlgItemTextA(d, IDC_F_DIR, dir);
      g_o.startmenu = IsWindowVisible(GetDlgItem(d, IDC_F_START)) && IsDlgButtonChecked(d, IDC_F_START) == BST_CHECKED;
      g_o.desktop = IsWindowVisible(GetDlgItem(d, IDC_F_DESK)) && IsDlgButtonChecked(d, IDC_F_DESK) == BST_CHECKED;
      g_o.context = IsWindowVisible(GetDlgItem(d, IDC_F_CTX)) && IsDlgButtonChecked(d, IDC_F_CTX) == BST_CHECKED;
      set_result(d, 0);
      return TRUE;
    }
    case PSN_QUERYCANCEL: set_result(d, query_cancel(d)); return TRUE;
    }
    break;
  }
  return FALSE;
}

static HWND make_ctl(HWND d, HFONT f, const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id)
{
  RECT r;
  HWND c;
  r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
  MapDialogRect(d, &r);
  c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left, r.bottom - r.top, d,
                      (HMENU)id, g_inst, 0);
  if (c) SendMessageA(c, WM_SETFONT, (WPARAM)f, 0);
  return c;
}

static void types_create(HWND d)
{
  TypesBox b[M_MAX_GROUPS];
  HFONT f = (HFONT)SendMessageA(d, WM_GETFONT, 0, 0);
  int g, i;
  types_layout(&g_m, b);
  SetDlgItemTextA(d, IDC_T_INTRO, g_m.types_intro);
  for (g = 0; g < g_m.ngroups; g++) {
    int k = 0;
    if (g_m.groups[g].note[0])
      make_ctl(d, f, "STATIC", g_m.groups[g].note, SS_LEFT, b[g].x + 7, b[g].y + 11, b[g].w - 14, 8, -1);
    make_ctl(d, f, "BUTTON", g_m.groups[g].title, BS_GROUPBOX, b[g].x, b[g].y, b[g].w, b[g].h, IDC_T_GROUP + g);
    for (i = 0; i < g_m.next; i++) {
      if (g_m.exts[i].group != g) continue;
      make_ctl(d, f, "BUTTON", g_m.exts[i].ext, BS_AUTOCHECKBOX | WS_TABSTOP, b[g].x + 7 + (k % b[g].cols) * 38,
               b[g].top + (k / b[g].cols) * 12, 36, 10, IDC_T_FIRST + i);
      k++;
    }
  }
}

static BOOL CALLBACK types_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  int i;
  switch (m) {
  case WM_INITDIALOG:
    page_init(d);
    types_create(d);
    for (i = 0; i < g_m.next; i++) CheckDlgButton(d, IDC_T_FIRST + i, g_o.types[i] ? BST_CHECKED : BST_UNCHECKED);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wp) == IDC_T_DEFAULTS) {
      for (i = 0; i < g_m.next; i++) CheckDlgButton(d, IDC_T_FIRST + i, g_m.exts[i].def ? BST_CHECKED : BST_UNCHECKED);
    } else if (LOWORD(wp) == IDC_T_NONE) {
      for (i = 0; i < g_m.next; i++) CheckDlgButton(d, IDC_T_FIRST + i, BST_UNCHECKED);
    }
    break;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE: wiz_buttons(d, IDD_TYPES, 1); set_result(d, 0); return TRUE;
    case PSN_WIZNEXT:
      for (i = 0; i < g_m.next; i++) g_o.types[i] = IsDlgButtonChecked(d, IDC_T_FIRST + i) == BST_CHECKED;
      set_result(d, 0);
      return TRUE;
    case PSN_QUERYCANCEL: set_result(d, query_cancel(d)); return TRUE;
    }
    break;
  }
  return FALSE;
}

static void progress_cb(int pct, const char* text, void* ctx)
{
  HWND d = (HWND)ctx;
  MSG mm;
  SendDlgItemMessageA(d, IDC_P_BAR, PBM_SETPOS, (WPARAM)pct, 0);
  SetDlgItemTextA(d, IDC_P_TEXT, text);
  while (PeekMessageA(&mm, 0, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageA(&mm);   // keep the window painted
  UpdateWindow(GetParent(d));
}

static BOOL CALLBACK progress_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  char text[400];
  (void)wp;
  switch (m) {
  case WM_INITDIALOG:
    page_init(d);
    wsprintfA(text, "Installing %s", g_m.name);
    SetDlgItemTextA(d, IDC_TITLE, text);
    SendDlgItemMessageA(d, IDC_P_BAR, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    return TRUE;
  case WM_APP_START:
    while (app_running(g_o.dir)) {
      slog("%s is running", g_m.name);
      running_text(text);
      if (msg(GetParent(d), text, MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY) {
        g_cancelled = 1;
        g_rc = RC_FAIL;
        slog("cancelled: the program is running");
        break;
      }
    }
    if (!g_cancelled) {
      g_ran = 1;
      g_rc = inst_run(&g_o, progress_cb, d, g_err, sizeof(g_err));
    }
    PropSheet_SetWizButtons(GetParent(d), PSWIZB_NEXT);
    PropSheet_PressButton(GetParent(d), PSBTN_NEXT);
    return TRUE;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE:
      PropSheet_SetWizButtons(GetParent(d), 0);
      EnableWindow(GetDlgItem(GetParent(d), IDCANCEL), FALSE);
      PostMessageA(d, WM_APP_START, 0, 0);
      set_result(d, 0);
      return TRUE;
    case PSN_QUERYCANCEL: set_result(d, TRUE); return TRUE;    // no cancel while files are copied
    }
    break;
  }
  return FALSE;
}

static BOOL CALLBACK finish_proc(HWND d, UINT m, WPARAM wp, LPARAM lp)
{
  char title[200], text[1400], ctx[100];
  (void)wp;
  switch (m) {
  case WM_INITDIALOG:
    page_init(d);
    wsprintfA(text, "&Start %s now", g_m.name);
    SetDlgItemTextA(d, IDC_D_RUN, text);
    return TRUE;
  case WM_NOTIFY:
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE:
      PropSheet_SetWizButtons(GetParent(d), PSWIZB_FINISH);
      EnableWindow(GetDlgItem(GetParent(d), IDCANCEL), FALSE);
      ShowWindow(GetDlgItem(d, IDC_D_RUN), SW_HIDE);
      ShowWindow(GetDlgItem(d, IDC_D_REBOOT), SW_HIDE);
      ShowWindow(GetDlgItem(d, IDC_D_README), SW_HIDE);
      if (g_cancelled) {
        s_copy(title, sizeof(title), "Setup was cancelled");
        wsprintfA(text, "Nothing was changed. Run Setup again when %s is closed.", g_m.name);
      } else if (g_rc == RC_OK || g_rc == RC_REBOOT) {
        if (g_mode == M_UPGRADE) wsprintfA(title, "%s was upgraded to %s", g_m.name, m_shown_version(&g_m));
        else wsprintfA(title, "%s %s is installed", g_m.name, m_shown_version(&g_m));
        wsprintfA(text, "%s is in %s.", g_m.name, g_o.dir);
        if (g_rc == RC_REBOOT) {
          wsprintfA(text + lstrlenA(text), "\n\nSome files were in use. Windows replaces them when it restarts; start "
                    "%s after the restart.", g_m.name);
          ShowWindow(GetDlgItem(d, IDC_D_REBOOT), SW_SHOW);
          CheckDlgButton(d, IDC_D_REBOOT, BST_CHECKED);
        } else {
          if (g_o.context) {
            int i, k = 0;
            for (i = 0; g_m.ctx_text[i] && k < (int)sizeof(ctx) - 1; i++) if (g_m.ctx_text[i] != '&') ctx[k++] = g_m.ctx_text[i];
            ctx[k] = 0;
            wsprintfA(text + lstrlenA(text), "\n\nRight-click any file and choose \"%s\".", ctx);
          }
          if (g_m.run) {
            ShowWindow(GetDlgItem(d, IDC_D_RUN), SW_SHOW);
            CheckDlgButton(d, IDC_D_RUN, BST_CHECKED);
          }
          if (g_m.readme[0]) {
            ShowWindow(GetDlgItem(d, IDC_D_README), SW_SHOW);
            CheckDlgButton(d, IDC_D_README, BST_UNCHECKED);
          }
        }
      } else {
        s_copy(title, sizeof(title), "Setup could not finish");
        s_copy(text, sizeof(text), g_err[0] ? g_err : "An error occurred.");
        s_cat(text, sizeof(text), "\n\nDetails are in the log: ");
        s_cat(text, sizeof(text), g_o.log);
      }
      SetDlgItemTextA(d, IDC_TITLE, title);
      SetDlgItemTextA(d, IDC_D_TEXT, text);
      set_result(d, 0);
      return TRUE;
    case PSN_WIZFINISH:
      g_do_run = IsWindowVisible(GetDlgItem(d, IDC_D_RUN)) && IsDlgButtonChecked(d, IDC_D_RUN) == BST_CHECKED;
      g_do_reboot = IsWindowVisible(GetDlgItem(d, IDC_D_REBOOT)) && IsDlgButtonChecked(d, IDC_D_REBOOT) == BST_CHECKED;
      g_do_readme = IsWindowVisible(GetDlgItem(d, IDC_D_README)) && IsDlgButtonChecked(d, IDC_D_README) == BST_CHECKED;
      set_result(d, 0);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

static int run_wizard()
{
  struct Pg { int id; DLGPROC proc; };
  Pg pg[6];
  PROPSHEETPAGEA psp[6];
  HPROPSHEETPAGE hp[6];
  PROPSHEETHEADERA psh;
  int i, n = 0;
  pg[n].id = IDD_WELCOME; pg[n++].proc = welcome_proc;
  if (g_m.license[0]) { pg[n].id = IDD_LICENSE; pg[n++].proc = license_proc; }
  pg[n].id = IDD_FOLDER; pg[n++].proc = folder_proc;
  if (g_m.has_types) { pg[n].id = IDD_TYPES; pg[n++].proc = types_proc; }
  g_last_page = pg[n - 1].id;
  pg[n].id = IDD_PROGRESS; pg[n++].proc = progress_proc;
  pg[n].id = IDD_FINISH; pg[n++].proc = finish_proc;
  for (i = 0; i < n; i++) {
    memset(&psp[i], 0, sizeof(psp[i]));
    psp[i].dwSize = sizeof(psp[i]);
    psp[i].dwFlags = PSP_USETITLE;
    psp[i].hInstance = g_inst;
    psp[i].pszTemplate = MAKEINTRESOURCE(pg[i].id);
    psp[i].pfnDlgProc = pg[i].proc;
    psp[i].pszTitle = g_caption;
    hp[i] = CreatePropertySheetPageA(&psp[i]);
    if (!hp[i]) { slog("error: CreatePropertySheetPage %d failed", i); return RC_FAIL; }
  }
  memset(&psh, 0, sizeof(psh));
  psh.dwSize = sizeof(psh);
  psh.dwFlags = PSH_WIZARD | PSH_USEHICON;
  psh.hInstance = g_inst;
  psh.hIcon = (HICON)LoadImageA(g_inst, MAKEINTRESOURCE(IDI_SETUP), IMAGE_ICON, 16, 16, 0);
  psh.pszCaption = g_caption;
  psh.nPages = n;
  psh.phpage = hp;
  if (PropertySheetA(&psh) < 0) slog("error: PropertySheet failed (%lu)", GetLastError());
  if (g_bold) DeleteObject(g_bold);
  if (!g_ran) return RC_FAIL;
  if (g_do_readme) launch_app(g_o.dir, 1);
  if (g_do_reboot) {
    slog("restarting Windows");
    log_close();
    ExitWindowsEx(EWX_REBOOT, 0);
  } else if (g_do_run) {
    launch_app(g_o.dir, 0);
  }
  return g_rc;
}

static int install_main()
{
  char bad[300], text[1200];
  int rc;
  detect_install(&g_ii);
  opts_defaults(&g_o, &g_ii);
  wsprintfA(g_caption, "%s %s Setup", g_m.name, m_shown_version(&g_m));
  slog("payload %s %s, %lu files (%lu bytes); installed %d (%s, version %s)", g_m.name, m_shown_version(&g_m), g_pay.nfiles,
       g_pay.total_usize, g_ii.installed, g_ii.dir, g_ii.ver);
  if (g_arg_start >= 0) g_o.startmenu = g_arg_start && g_m.startmenu != OPT_OFF && g_m.nlinks;
  if (g_arg_desk >= 0) g_o.desktop = g_arg_desk && g_m.desktop != OPT_OFF && g_m.ndlinks;
  if (g_arg_ctx >= 0) g_o.context = g_arg_ctx && g_m.has_context;
  if (!apply_types(g_arg_types, bad, sizeof(bad))) {
    slog("error: unknown file type %s", bad);
    if (!g_o.silent) { wsprintfA(text, "Unknown file type in /TYPES: %s", bad); msg(0, text, MB_OK | MB_ICONSTOP); }
    return RC_FAIL;
  }
  if (g_o.dir_given) s_copy(g_o.dir, sizeof(g_o.dir), g_arg_dir);
  if (g_o.dir_given && g_ii.installed && !p_same(g_o.dir, g_ii.dir)) {
    slog("error: installed in %s, /D asks for another folder", g_ii.dir);
    wsprintfA(text, "%s is already installed in %s. Uninstall it first to install it into another folder.", g_m.name,
              g_ii.dir);
    if (!g_o.silent) msg(0, text, MB_OK | MB_ICONSTOP);
    return RC_FAIL;
  }
  if (g_ii.installed) s_copy(g_o.dir, sizeof(g_o.dir), g_ii.dir);
  g_mode = compute_mode(g_o.dir, g_oldver);
  g_need_kb = space_needed_kb(g_o.dir);
  slog("mode %s%s, %lu KB needed", g_mode == M_UPGRADE ? "upgrade from " : g_mode == M_REPAIR ? "repair" :
       g_mode == M_DOWNGRADE ? "downgrade from " : "fresh install", g_mode == M_UPGRADE || g_mode == M_DOWNGRADE ?
       g_oldver : "", g_need_kb);
  if (!check_requirements(text, sizeof(text) - 100)) {
    slog("requirements not met: %s", text);
    if (!g_o.silent) {
      s_cat(text, sizeof(text), "\nInstall it first, or continue anyway?");
      if (msg(0, text, MB_YESNO | MB_DEFBUTTON2 | MB_ICONEXCLAMATION) != IDYES) { slog("cancelled"); return RC_FAIL; }
    }
  }
  if (!g_o.silent) return run_wizard();

  if (g_mode == M_DOWNGRADE && !g_o.force) {
    slog("refused: %s is newer than %s (use /FORCE)", g_oldver, m_shown_version(&g_m));
    return RC_DOWNGRADE;
  }
  {
    u32 kb;
    if (space_free_kb(g_o.dir, &kb) && kb < g_need_kb) {
      slog("refused: %lu KB free, %lu KB needed", kb, g_need_kb);
      return RC_DISKSPACE;
    }
  }
  if (app_running(g_o.dir)) {
    slog("refused: %s is running (close it and run setup again)", g_m.name);
    return RC_RUNNING;
  }
  rc = inst_run(&g_o, 0, 0, g_err, sizeof(g_err));
  if (rc == RC_FAIL) slog("error: %s", g_err);
  if (rc == RC_OK && g_o.run_after) launch_app(g_o.dir, 0);
  return rc;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
  char bad[300], text[1600], perr[300], mname[80], logname[16];
  HANDLE mutex;
  int ok, rc, have;
  (void)prev; (void)cmdline; (void)show;
  g_inst = inst;
  InitCommonControls();
  ok = parse_args(bad, sizeof(bad));
  have = pay_open(perr, sizeof(perr));
  s_copy(g_caption, sizeof(g_caption), have ? g_m.name : "Setup98");
  s_cat(g_caption, sizeof(g_caption), " Setup");
  if (!have) {
    if (!g_o.silent) {
      if (perr[0]) msg(0, perr, MB_OK | MB_ICONSTOP);
      else msg(0, "This is the Setup98 " S98_VERSION_STR " stub. It has no product inside: build an installer with "
                  "pack98 (see M:\\dev\\setup98\\README.md).", MB_OK | MB_ICONINFORMATION);
    }
    return RC_FAIL;
  }
  if (!g_o.log[0]) {
    m_log_name(&g_m, logname);
    s_cat(logname, sizeof(logname), ".LOG");
    GetTempPathA(sizeof(g_o.log), g_o.log);
    p_join(g_o.log, logname, g_o.log, sizeof(g_o.log));
  }
  log_open(g_o.log, g_o.child_pid == 0);
  slog("command line: %s", g_args_raw);
  if (!ok) {
    slog("error: unknown switch %s", bad);
    if (!g_o.silent) {
      wsprintfA(text, "Unknown switch: %s\n\n", bad);
      usage_text(text + lstrlenA(text), sizeof(text) - lstrlenA(text));
      msg(0, text, MB_OK | MB_ICONINFORMATION);
    }
    log_close();
    return RC_FAIL;
  }
  if (g_o.fixnames) {                // RunOnce after a restart (see inst.cpp): no UI
    char mdir[MAX_PATH];
    p_module_dir(mdir, sizeof(mdir));
    slog("restored %d long file names in %s", fix_names(mdir), mdir);
    log_close();
    return RC_OK;
  }
  wsprintfA(mname, "Setup98_%s_Running", g_m.id);
  mutex = CreateMutexA(0, FALSE, mname);
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && !g_o.child_pid) {
    slog("error: another setup is running");
    wsprintfA(text, "%s Setup is already running.", g_m.name);
    if (!g_o.silent) msg(0, text, MB_OK | MB_ICONINFORMATION);
    log_close();
    return RC_FAIL;
  }
  CoInitialize(0);
  if (g_o.uninstall || g_pay.nfiles == 0) rc = uninstall_main();
  else rc = install_main();
  CoUninitialize();
  slog("exit code %d", rc);
  log_close();
  if (mutex) CloseHandle(mutex);
  return rc;
}
