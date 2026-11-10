// s98.h - Setup98 stub (setup98.exe): declarations shared by setup.cpp (command line, wizard, flows), inst.cpp
// (install/uninstall work), assoc.cpp (file types, context menu), payload.cpp (own payload, install record) and
// util.cpp (strings, paths, log, registry). Include <windows.h> first. VC6 dialect, ANSI Win98-safe APIs only.
#ifndef S98_STUB_H
#define S98_STUB_H
#include "s98types.h"
#include "manifest.h"
#include "pformat.h"

// exit codes (README.md "Switches and exit codes")
enum {
  RC_OK = 0,          // done
  RC_FAIL = 1,        // error, bad switch or cancelled
  RC_RUNNING = 2,     // the program is running (silent mode never waits and never kills it)
  RC_REBOOT = 3,      // done, but files were in use: WININIT.INI replaces them when Windows restarts
  RC_DOWNGRADE = 4,   // a newer version is installed (silent mode without /FORCE)
  RC_DISKSPACE = 5    // not enough free disk space (silent mode)
};

struct Opts {
  char dir[MAX_PATH];     // install folder (no trailing backslash)
  int startmenu;          // Start menu shortcuts
  int desktop;            // desktop shortcut(s)
  int context;            // HKCR\*\shell\<Id>
  int types[M_MAX_EXT];   // per manifest extension
  // switches
  int silent, force, purge, locktest, run_after, uninstall, dir_given, fixnames;
  DWORD child_pid;        // /_CHILD=<pid>: the temp copy of the uninstaller started by the installed one
  char log[MAX_PATH];
};

struct InstallInfo {
  int installed;          // HKLM\Software\<Id>\InstallDir present
  char dir[MAX_PATH];
  int have_ver;
  DWORD ms, ls;
  char ver[32];
};

extern Manifest g_m;      // the manifest of this setup (payload), or of the installed copy while uninstalling

// ---- util.cpp: strings, paths, log, registry ----
void s_copy(char* dst, int cap, const char* src);
void s_cat(char* dst, int cap, const char* src);
int  starts_i(const char* s, const char* prefix);
void p_join(const char* dir, const char* name, char* out, int cap);
int  p_exists(const char* path);
int  p_is_dir(const char* path);
void p_module_dir(char* out, int cap);
int  p_same(const char* a, const char* b);                 // same file/folder (short-path compare)
int  p_normalize_dir(const char* in, char* out, int cap);  // absolute, no trailing '\', not a root
int  make_dirs(const char* dir);
void expand_folder(const char* tmpl, char* out, int cap);  // %ProgramFiles%, %WinDrive%
int  special_dir(int csidl, char* out);

void log_open(const char* path, int truncate);
void log_close();
void slog(const char* fmt, ...);

int   reg_get(HKEY root, const char* key, const char* name, char* out, int cap);   // 1 value, 0 no value, -1 no key
DWORD reg_get_dw(HKEY root, const char* key, const char* name, DWORD def);
int   reg_key_exists(HKEY root, const char* key);
int   reg_key_empty(HKEY root, const char* key);
int   reg_set(HKEY root, const char* key, const char* name, const char* val);
int   reg_set_dw(HKEY root, const char* key, const char* name, DWORD v);
void  reg_del_value(HKEY root, const char* key, const char* name);
void  reg_del_tree(HKEY root, const char* key);
int   reg_del_if_empty(HKEY root, const char* key);
int   reg_value_names(HKEY root, const char* key, char names[][260], int max);
const char* root_name(HKEY root);
void  key_app(char* out);                                  // Software\<Id>
void  key_uninst(char* out);                               // Software\Microsoft\Windows\CurrentVersion\Uninstall\<Id>

// WININIT.INI (Win9x replaces/deletes files there at the next start)
int  wininit_line(const char* dst, const char* src);
int  wininit_flush();
void wininit_reset();
int  wininit_delete_later(const char* path);
void delete_file(const char* path, int* reboot);

// ---- payload.cpp: this exe's payload, the install record ----
struct Payload {
  int present;            // a Setup98 trailer was found
  HANDLE f;
  PfTrailer t;
  u32 nfiles;
  PfEntry* files;
  char* manifest;         // canonical manifest text (0-terminated)
  u32 msize;
  u32 total_usize;
  u32 max_usize, max_csize;
};
extern Payload g_pay;
int  pay_open(char* err, int errcap);                      // 1 = payload read and verified (CRC), manifest parsed
int  pay_find(const char* name);                           // index of a packed file or -1
// extracts entry i into buf (usize bytes; cbuf >= csize scratch); 1 ok, 0 read error, -1 damaged (CRC)
int  pay_extract(int i, u8* buf, u8* cbuf);
int  pay_write_uninstaller(const char* path, u32* crc);    // stub + manifest-only payload
u8*  pay_load_text(const char* name, u32* n);              // a packed text file, CRLF, 0-terminated (HeapFree it)

enum { RF_PROGRAM = 'p', RF_KEEP = 'k' };
struct RecFile {
  char name[PF_MAX_NAME + 1];
  char kind;              // RF_PROGRAM, RF_KEEP
  int has_crc;
  u32 crc;
};
struct Record {
  int loaded;             // from <dir>\<record>; 0 = only what the registry knew (Pica 1.0 style)
  int has_manifest;
  Manifest* m;            // the manifest of the installed copy (when has_manifest)
  RecFile* files;
  int nfiles, capfiles;
  char (*dirs)[PF_MAX_NAME + 1];   // folders Setup created, relative to the install folder
  int ndirs, capdirs;
  char links[2 * M_MAX_LINKS + 2][MAX_PATH];   // shortcut paths; entries starting with '>' are folders we created
  int nlinks;
};
void rec_init(Record* r);
void rec_free(Record* r);
int  rec_load(Record* r, const char* dir);                 // record file, else registry Files; 1 = something found
int  rec_save(const Record* r, const Manifest* m, const Opts* o, const char* renames);
int  fix_names(const char* dir);                           // RunOnce /_FIXNAMES after WININIT.INI renames
int  rec_add_file(Record* r, const char* name, char kind, int has_crc, u32 crc);
int  rec_find_file(const Record* r, const char* name);
int  rec_add_dir(Record* r, const char* rel);
int  rec_find_dir(const Record* r, const char* rel);

// ---- inst.cpp ----
int  file_version(const char* path, DWORD* ms, DWORD* ls);
void detect_install(InstallInfo* ii);
void opts_defaults(Opts* o, const InstallInfo* ii);
void opts_default_types(Opts* o);
int  app_running(const char* dir);                         // window class/title, or the main exe in use
int  userdata_exists(const char* dir);                     // anything the purge question would delete
u32  space_needed_kb(const char* dir);                     // for the files this setup writes (cluster-rounded)
int  space_free_kb(const char* dir, u32* kb);              // 1 = known
int  check_requirements(char* msg, int cap);               // 1 = met; else msg says what is missing
typedef void (*ProgressFn)(int pct, const char* text, void* ctx);
int  inst_run(const Opts* o, ProgressFn fn, void* ctx, char* err, int errcap);
int  uninst_run(const char* dir, int purge, int defer_self, int* kept_folder);
int  uninst_finish(const char* dir);                       // delete the uninstaller, remove the folder if empty
void notify_shell();

// ---- assoc.cpp ----
void our_command(const char* dir, char* out, int cap);
int  assoc_apply(const Manifest* m, const char* dir, const int* types);   // all zero = remove everything
void created_cleanup();
int  context_set(const Manifest* m, int want, const char* cmd);
void legacy_cleanup(const Manifest* m);

#endif
