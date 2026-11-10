// manifest.h - Setup98 manifest model (portable C++98). pack98 parses the project's manifest strictly and writes a
// canonical copy into the payload; the stub parses that canonical copy (and the install record) with the same code.
// Every key is documented in README.md ("Manifest reference").
#ifndef S98_MANIFEST_H
#define S98_MANIFEST_H
#include "s98types.h"

enum {
  M_PATH = 260,
  M_NAME = 81,
  M_TEXT = 512,
  M_MAX_RULES = 64,
  M_MAX_PAT = 32,
  M_MAX_LINKS = 12,
  M_MAX_GROUPS = 8,
  M_MAX_EXT = 48
};

enum { R_SOURCE, R_FILE, R_KEEP, R_EXCLUDE };                 // [Files] rule kinds (pack98 only)
enum { OPT_OFF = -1, OPT_NO = 0, OPT_YES = 1 };               // StartMenu / Desktop: not offered, unticked, ticked
enum { WIN_ANY = 0, WIN_95, WIN_98, WIN_98SE, WIN_ME };

enum {
  MP_PACK = 1,        // the project's manifest: [Files] rules, Version=from:, [Pack] allowed; File= required
  MP_RECORD = 2       // an install record: skip [Installed*] sections
};

struct MRule {
  int kind;
  char pat[M_PATH];
  char as[M_PATH];       // install under this name (single file only)
  int optional;
  int line;
};

struct MLink {
  char name[M_NAME];     // file name of the .lnk without extension
  char target[M_PATH];   // installed file, relative to the install folder
  char args[M_PATH];
  char desc[M_NAME];
  char icon[M_PATH];     // "file[,index]" relative to the install folder; empty = the target's own
  int line;
};

struct MGroup {
  char title[M_NAME];
  char note[M_NAME * 2];
  int edit;              // script types: the program becomes the Edit verb while Open keeps running them
};

struct MExt {
  char ext[16];          // ".txt"
  int group;
  int def;               // ticked by default
};

struct Manifest {
  // [Product]
  char id[41];
  char name[M_NAME];
  char version[32];
  char display_version[41];    // optional free text shown instead of version ("1.2 RC2"); comparisons use version
  char version_from[M_PATH];   // pack98: Version=from:<file> (resolved into version)
  u32 ver_ms, ver_ls;
  char publisher[M_NAME];
  char mainexe[M_PATH];
  char folder[M_PATH];
  char description[M_TEXT];
  char readme[M_PATH];
  char license[M_PATH];
  int license_agree;
  int run;
  char run_args[M_PATH];
  char window_class[M_NAME];
  char window_title[M_NAME * 2];
  char uninstaller[64];
  char data_name[M_NAME * 2];
  char logname[9];
  char url[M_PATH];
  // [Files]
  MRule rules[M_MAX_RULES];
  int nrules;
  char userdata[M_MAX_PAT][M_PATH];
  int nuserdata;
  char generated[M_MAX_PAT][M_PATH];
  int ngenerated;
  // [Shortcuts]
  int startmenu, desktop;
  char group[M_NAME];
  int group_set;
  MLink links[M_MAX_LINKS];
  int nlinks;
  MLink dlinks[M_MAX_LINKS];
  int ndlinks;
  // [Requires]
  int req_dx;
  int req_win;
  // [Types]
  int has_types;
  MGroup groups[M_MAX_GROUPS];
  int ngroups;
  MExt exts[M_MAX_EXT];
  int next;
  char types_intro[M_NAME * 3];
  char legacy_progid[M_NAME];
  char type_icon[M_PATH];
  char edit_verb[M_NAME];
  // [ContextMenu]
  int has_context;
  char ctx_text[M_NAME];
  int ctx_default;
  // [Pack] (pack98 only, never in the payload)
  char outfile[M_PATH];
  char stub[M_PATH];
  // pack98 bookkeeping: line numbers for messages after the file list is known
  int line_mainexe, line_readme, line_license, line_version, line_icon;
};

// parses text (any line endings) into m; strict validation. Returns 1, or 0 with "line N: message" in err.
int manifest_parse(const char* text, u32 len, Manifest* m, int flags, char* err, int errcap);
// fills the defaults that depend on other keys (Folder, Group, links, LogName, ...); manifest_parse calls it
void manifest_finish(Manifest* m);
// canonical text (CRLF) without [Pack] and the [Files] rules; returns the length, or 0 if cap is too small
u32 manifest_write(const Manifest* m, char* out, u32 cap);

// versions
int ver_parse(const char* s, u32* ms, u32* ls);           // "1.2.3" -> 1; 0 if malformed
void ver_format(u32 ms, u32 ls, char* out);               // "1.0", "1.2.3", "1.2.3.4" (out >= 24 bytes)
const char* m_shown_version(const Manifest* m);           // DisplayVersion if set, else Version
int ver_compare(u32 ams, u32 als, u32 bms, u32 bls);
int find_fixed_version(const u8* exe, u32 n, u32* ms, u32* ls);   // VS_FIXEDFILEINFO file version

// 8.3-safe names derived from the Id
void m_log_name(const Manifest* m, char* out);            // "PICASET" (+ ".LOG")
void m_temp_name(const Manifest* m, char* out);           // "PICAUN" (+ ".EXE"), the uninstaller's temp copy
void m_record_name(const Manifest* m, char* out);         // "uninst.ini" (uninstaller name with .ini)

// File types page layout in dialog units (page 317 x 160): one box per group
struct TypesBox {
  int x, y, w, h, top, cols;
};
int types_layout(const Manifest* m, TypesBox* boxes);     // 1 = fits on the page

#endif
