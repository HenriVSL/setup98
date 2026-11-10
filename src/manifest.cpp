// manifest.cpp - Setup98 manifest parser, validator and canonical writer (see manifest.h, README.md).
// Portable C++98, no Windows headers: pack98 (VC6 and clang) and the stub share it.
#include <string.h>
#include "manifest.h"
#include "wild.h"
#include "port.h"

// ---- small string helpers ----

static int ieq(const char* a, const char* b)
{
  return path_cmp_i(a, b) == 0;
}

static int istarts(const char* s, const char* prefix)
{
  while (*prefix) {
    char a = *s, b = *prefix;
    if (a >= 'a' && a <= 'z') a = (char)(a - 32);
    if (b >= 'a' && b <= 'z') b = (char)(b - 32);
    if (a != b) return 0;
    s++;
    prefix++;
  }
  return 1;
}

static int scopy(char* dst, int cap, const char* src)
{
  int n = (int)strlen(src);
  if (n >= cap) {
    memcpy(dst, src, (size_t)(cap - 1));
    dst[cap - 1] = 0;
    return 0;
  }
  memcpy(dst, src, (size_t)n + 1);
  return 1;
}

static void trim(char* s)
{
  int n = (int)strlen(s), i = 0;
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  while (s[i] == ' ' || s[i] == '\t') i++;
  if (i) memmove(s, s + i, (size_t)(n - i) + 1);
}

static int is_alnum(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// ---- versions ----

int ver_parse(const char* s, u32* ms, u32* ls)
{
  u32 part[4];
  int i = 0, digits = 0;
  part[0] = part[1] = part[2] = part[3] = 0;
  if (!*s) return 0;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') {
      part[i] = part[i] * 10 + (u32)(*s - '0');
      if (part[i] > 65535 || ++digits > 5) return 0;
    } else if (*s == '.') {
      if (!digits || ++i > 3) return 0;
      digits = 0;
    } else {
      return 0;
    }
  }
  if (!digits) return 0;
  *ms = (part[0] << 16) | part[1];
  *ls = (part[2] << 16) | part[3];
  return 1;
}

void ver_format(u32 ms, u32 ls, char* out)
{
  if (ls & 0xFFFF) s98_fmt(out, 24, "%u.%u.%u.%u", ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF);
  else if (ls >> 16) s98_fmt(out, 24, "%u.%u.%u", ms >> 16, ms & 0xFFFF, ls >> 16);
  else s98_fmt(out, 24, "%u.%u", ms >> 16, ms & 0xFFFF);
}

int ver_compare(u32 ams, u32 als, u32 bms, u32 bls)
{
  if (ams != bms) return ams < bms ? -1 : 1;
  if (als != bls) return als < bls ? -1 : 1;
  return 0;
}

int find_fixed_version(const u8* p, u32 n, u32* ms, u32* ls)
{
  u32 i;
  if (n < 64) return 0;
  for (i = 0; i + 52 <= n; i++) {
    if (p[i] == 0xBD && p[i + 1] == 0x04 && p[i + 2] == 0xEF && p[i + 3] == 0xFE) {
      *ms = get_le32(p + i + 8);
      *ls = get_le32(p + i + 12);
      return 1;
    }
  }
  return 0;
}

// ---- derived names ----

void m_log_name(const Manifest* m, char* out)
{
  int i, k = 0;
  if (m->logname[0]) { scopy(out, 9, m->logname); return; }
  for (i = 0; m->id[i] && k < 5; i++) {
    char c = m->id[i];
    if (!is_alnum(c)) continue;
    out[k++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  out[k] = 0;
  scopy(out + k, 9 - k, "SET");
}

void m_temp_name(const Manifest* m, char* out)
{
  int i, k = 0;
  for (i = 0; m->id[i] && k < 6; i++) {
    char c = m->id[i];
    if (!is_alnum(c)) continue;
    out[k++] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  out[k] = 0;
  scopy(out + k, 9 - k, "UN");
}

void m_record_name(const Manifest* m, char* out)
{
  char* dot;
  scopy(out, 64, m->uninstaller[0] ? m->uninstaller : "uninst.exe");
  dot = strrchr(out, '.');
  if (dot) *dot = 0;
  scopy(out + strlen(out), 64 - (int)strlen(out), ".ini");
}

// ---- validation helpers ----

// text: no control characters ('\n' allowed only when multiline); bytes >= 0x80 are Windows-1252 letters
static const char* check_text(const char* v, int multiline)
{
  for (; *v; v++) {
    unsigned char c = (unsigned char)*v;
    if (c == '\n' && multiline) continue;
    if (c < 32 || c == 127) return "control characters are not allowed";
  }
  return 0;
}

static int bad_name_char(char c)
{
  return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
}

// a single file or folder name (links, Group, Name)
static const char* check_filename(const char* v, int allow_empty)
{
  int n = (int)strlen(v);
  const char* e = check_text(v, 0);
  if (e) return e;
  if (!n) return allow_empty ? 0 : "must not be empty";
  for (; *v; v++) if (bad_name_char(*v)) return "must not contain \\ / : * ? \" < > |";
  if (v[-1] == '.' || v[-1] == ' ') return "must not end with a dot or a space";
  return 0;
}

// relative install path or pattern: ASCII, '\' or '/' separators (normalized to '\'), no drive, no "..", no empty
// parts. Wildcards only when wild.
static const char* check_relpath(char* v, int wild)
{
  char* p;
  int n = (int)strlen(v);
  if (!n) return "must not be empty";
  if (n > 200) return "path is too long (at most 200 characters)";
  for (p = v; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (*p == '/') *p = '\\';
    if (c < 32 || c > 126) return "paths must be plain ASCII";
    if (*p == ':' || *p == '"' || *p == '<' || *p == '>' || *p == '|') return "path contains : \" < > or |";
    if (!wild && (*p == '*' || *p == '?')) return "wildcards are not allowed here";
  }
  if (v[0] == '\\') return "must be relative (no leading \\)";
  if (v[n - 1] == '\\') return "must not end with \\";
  p = v;
  while (*p) {
    char* e = p;
    int len;
    while (*e && *e != '\\') e++;
    len = (int)(e - p);
    if (len == 0) return "empty folder name (\\\\)";
    if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return ". and .. are not allowed";
    if (p[len - 1] == ' ' || p[len - 1] == '.') return "a name ends with a dot or a space";
    p = *e ? e + 1 : e;
  }
  return 0;
}

// UserData / Generated: a relative pattern, optionally after %AppData%\ or %WinDir%\ (Pica keeps settings there)
static const char* check_datapat(char* v)
{
  if (istarts(v, "%AppData%\\") || istarts(v, "%AppData%/")) return check_relpath(v + 10, 1);
  if (istarts(v, "%WinDir%\\") || istarts(v, "%WinDir%/")) return check_relpath(v + 9, 1);
  if (v[0] == '%') return "only %AppData%\\ and %WinDir%\\ may start a pattern";
  return check_relpath(v, 1);
}

static const char* check_folder(char* v)
{
  char* rest;
  char* p;
  if (istarts(v, "%ProgramFiles%")) rest = v + 14;
  else if (istarts(v, "%WinDrive%")) rest = v + 10;
  else if (((v[0] >= 'A' && v[0] <= 'Z') || (v[0] >= 'a' && v[0] <= 'z')) && v[1] == ':' && (v[2] == '\\' || v[2] == '/'))
    rest = v + 2;
  else return "must start with %ProgramFiles%, %WinDrive% or a drive like C:\\";
  if (strlen(v) > 200) return "folder is too long (at most 200 characters)";
  if (check_text(v, 0)) return "control characters are not allowed";
  for (p = rest; *p; p++) {
    if (*p == '/') *p = '\\';
    if (*p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
      return "folder contains : * ? \" < > or |";
  }
  if (rest[0] && rest[0] != '\\') return "a \\ must follow the drive or %token%";
  p = rest;
  if (*p == '\\') p++;
  if (!*p) return "must name a folder, not a drive root";
  if (v[strlen(v) - 1] == '\\') return "must not end with \\";
  if (strstr(rest, "\\\\")) return "empty folder name (\\\\)";
  return 0;
}

static const char* check_bool(const char* v, int* out)
{
  if (!strcmp(v, "0") || ieq(v, "no") || ieq(v, "false")) { *out = 0; return 0; }
  if (!strcmp(v, "1") || ieq(v, "yes") || ieq(v, "true")) { *out = 1; return 0; }
  return "must be 0 or 1";
}

static const char* check_opt3(const char* v, int* out)
{
  if (ieq(v, "off") || ieq(v, "none")) { *out = OPT_OFF; return 0; }
  return check_bool(v, out) ? "must be 1 (ticked), 0 (unticked) or off (not offered)" : 0;
}

static const char* check_ident(const char* v, int maxlen)
{
  int n = (int)strlen(v);
  if (!n) return "must not be empty";
  if (n > maxlen) return "too long";
  for (; *v; v++) if (!is_alnum(*v) && *v != '_' && *v != '-') return "only letters, digits, _ and - are allowed";
  return 0;
}

// ---- UTF-8 -> Windows-1252 (pack98 reads manifests written on the Mac) ----

static int cp1252_from_ucs(u32 c)
{
  static const u16 hi[32] = { 0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                              0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013,
                              0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178 };
  int i;
  if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) return (int)c;
  for (i = 0; i < 32; i++) if (hi[i] && hi[i] == c) return 0x80 + i;
  return -1;
}

// decodes one UTF-8 sequence at s (n bytes left); returns its length or 0 if malformed
static int utf8_seq(const u8* s, u32 n, u32* cp)
{
  u32 c = s[0];
  int len, i;
  if (c < 0x80) { *cp = c; return 1; }
  if (c >= 0xC2 && c <= 0xDF) { len = 2; c &= 0x1F; }
  else if (c >= 0xE0 && c <= 0xEF) { len = 3; c &= 0x0F; }
  else if (c >= 0xF0 && c <= 0xF4) { len = 4; c &= 0x07; }
  else return 0;
  if ((u32)len > n) return 0;
  for (i = 1; i < len; i++) {
    if ((s[i] & 0xC0) != 0x80) return 0;
    c = (c << 6) | (s[i] & 0x3F);
  }
  if ((len == 3 && c < 0x800) || (len == 4 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return 0;
  *cp = c;
  return len;
}

// ---- sections and keys ----

enum { S_NONE, S_PRODUCT, S_FILES, S_SHORTCUTS, S_REQUIRES, S_TYPES, S_CONTEXT, S_PACK, S_SKIP, S_COUNT };
static const char* const g_secname[S_COUNT] = { "", "Product", "Files", "Shortcuts", "Requires", "Types", "ContextMenu",
                                                "Pack", "" };

enum {
  K_ID, K_NAME, K_VERSION, K_PUBLISHER, K_MAINEXE, K_FOLDER, K_DESC, K_README, K_LICENSE, K_AGREE, K_RUN, K_RUNARGS,
  K_WCLASS, K_WTITLE, K_UNINST, K_DATANAME, K_LOGNAME, K_URL,
  K_SOURCE, K_FILE, K_KEEP, K_EXCLUDE, K_USERDATA, K_GENERATED,
  K_STARTMENU, K_DESKTOP, K_GROUP, K_LINK, K_DLINK,
  K_DX, K_WIN,
  K_TGROUP, K_INTRO, K_LEGACY, K_TICON, K_EDITVERB,
  K_CTEXT, K_CDEFAULT,
  K_OUTFILE, K_STUB,
  K_COUNT
};

struct KeyDef {
  int sec;
  const char* name;
  int multi;
  int pack_only;
};

static const KeyDef g_keys[K_COUNT] = {
  { S_PRODUCT, "Id", 0, 0 }, { S_PRODUCT, "Name", 0, 0 }, { S_PRODUCT, "Version", 0, 0 },
  { S_PRODUCT, "Publisher", 0, 0 }, { S_PRODUCT, "MainExe", 0, 0 }, { S_PRODUCT, "Folder", 0, 0 },
  { S_PRODUCT, "Description", 0, 0 }, { S_PRODUCT, "Readme", 0, 0 }, { S_PRODUCT, "License", 0, 0 },
  { S_PRODUCT, "LicenseAgree", 0, 0 }, { S_PRODUCT, "Run", 0, 0 }, { S_PRODUCT, "RunArgs", 0, 0 },
  { S_PRODUCT, "WindowClass", 0, 0 }, { S_PRODUCT, "WindowTitle", 0, 0 }, { S_PRODUCT, "Uninstaller", 0, 0 },
  { S_PRODUCT, "DataName", 0, 0 }, { S_PRODUCT, "LogName", 0, 0 }, { S_PRODUCT, "URL", 0, 0 },
  { S_FILES, "Source", 1, 1 }, { S_FILES, "File", 1, 1 }, { S_FILES, "Keep", 1, 1 }, { S_FILES, "Exclude", 1, 1 },
  { S_FILES, "UserData", 1, 0 }, { S_FILES, "Generated", 1, 0 },
  { S_SHORTCUTS, "StartMenu", 0, 0 }, { S_SHORTCUTS, "Desktop", 0, 0 }, { S_SHORTCUTS, "Group", 0, 0 },
  { S_SHORTCUTS, "Link", 1, 0 }, { S_SHORTCUTS, "DesktopLink", 1, 0 },
  { S_REQUIRES, "DirectX", 0, 0 }, { S_REQUIRES, "Windows", 0, 0 },
  { S_TYPES, "Group", 1, 0 }, { S_TYPES, "Intro", 0, 0 }, { S_TYPES, "LegacyProgId", 0, 0 }, { S_TYPES, "Icon", 0, 0 },
  { S_TYPES, "EditVerb", 0, 0 },
  { S_CONTEXT, "Text", 0, 0 }, { S_CONTEXT, "Default", 0, 0 },
  { S_PACK, "OutFile", 0, 1 }, { S_PACK, "Stub", 0, 1 }
};

struct Parser {
  Manifest* m;
  int flags;
  int line;
  char* err;
  int errcap;
  int seen[K_COUNT];
  int secseen[S_COUNT];
};

static int fail(Parser* p, const char* msg)
{
  if (p->line) s98_fmt(p->err, p->errcap, "line %d: %s", p->line, msg);
  else s98_fmt(p->err, p->errcap, "%s", msg);
  return 0;
}

static int failk(Parser* p, const char* key, const char* msg)
{
  char buf[400];
  s98_fmt(buf, sizeof(buf), "%s: %s", key, msg);
  return fail(p, buf);
}

static int set_str(Parser* p, const char* key, char* dst, int cap, const char* val)
{
  char buf[64];
  if (!scopy(dst, cap, val)) {
    s98_fmt(buf, sizeof(buf), "value is too long (at most %d characters)", cap - 1);
    return failk(p, key, buf);
  }
  return 1;
}

// splits "a | b | c" into trimmed parts (in place); returns the count
static int split_bar(char* s, char** parts, int max)
{
  int n = 0;
  char* p = s;
  for (;;) {
    char* e = strchr(p, '|');
    if (n == max) return max + 1;
    if (e) *e = 0;
    trim(p);
    parts[n++] = p;
    if (!e) break;
    p = e + 1;
  }
  return n;
}

static int parse_rule(Parser* p, int kind, const char* key, char* val)
{
  Manifest* m = p->m;
  MRule* r;
  char* parts[8];
  int n, i;
  const char* e;
  if (m->nrules >= M_MAX_RULES) return failk(p, key, "too many [Files] lines (at most 64)");
  r = &m->rules[m->nrules];
  memset(r, 0, sizeof(*r));
  r->kind = kind;
  r->line = p->line;
  if (kind == R_SOURCE) {
    if (!val[0]) return failk(p, key, "must not be empty (use . for the manifest's folder)");
    if (check_text(val, 0)) return failk(p, key, "control characters are not allowed");
    if (!set_str(p, key, r->pat, M_PATH, val)) return 0;
    m->nrules++;
    return 1;
  }
  n = split_bar(val, parts, 7);
  if (n > 7) return failk(p, key, "too many | options");
  e = check_relpath(parts[0], 1);
  if (e) return failk(p, key, e);
  if (!set_str(p, key, r->pat, M_PATH, parts[0])) return 0;
  for (i = 1; i < n; i++) {
    if (kind == R_EXCLUDE) return failk(p, key, "Exclude takes no options");
    if (ieq(parts[i], "optional")) r->optional = 1;
    else if (istarts(parts[i], "as=")) {
      char* a = parts[i] + 3;
      trim(a);
      e = check_relpath(a, 0);
      if (e) return failk(p, "as", e);
      if (wild_has(r->pat)) return failk(p, key, "as= needs a single file, not a pattern");
      if (!set_str(p, "as", r->as, M_PATH, a)) return 0;
    } else {
      char buf[200];
      s98_fmt(buf, sizeof(buf), "unknown option \"%.100s\" (optional, as=<name>)", parts[i]);
      return failk(p, key, buf);
    }
  }
  m->nrules++;
  return 1;
}

static int parse_link(Parser* p, const char* key, char* val, MLink* l)
{
  char* parts[8];
  int n, i;
  const char* e;
  memset(l, 0, sizeof(*l));
  l->line = p->line;
  n = split_bar(val, parts, 7);
  if (n > 7) return failk(p, key, "too many | options");
  if (n < 2) return failk(p, key, "expected <name> | <target> [| args=...] [| desc=...] [| icon=...]");
  e = check_filename(parts[0], 0);
  if (e) return failk(p, key, e);
  if (!set_str(p, key, l->name, M_NAME, parts[0])) return 0;
  e = check_relpath(parts[1], 0);
  if (e) return failk(p, key, e);
  if (!set_str(p, key, l->target, M_PATH, parts[1])) return 0;
  for (i = 2; i < n; i++) {
    char* v = strchr(parts[i], '=');
    if (!v) return failk(p, key, "options are args=, desc= and icon=");
    *v++ = 0;
    trim(parts[i]);
    trim(v);
    if (check_text(v, 0)) return failk(p, key, "control characters are not allowed");
    if (ieq(parts[i], "args")) { if (!set_str(p, "args", l->args, M_PATH, v)) return 0; }
    else if (ieq(parts[i], "desc")) { if (!set_str(p, "desc", l->desc, M_NAME, v)) return 0; }
    else if (ieq(parts[i], "icon")) {
      char file[M_PATH];
      char* comma;
      if (!set_str(p, "icon", l->icon, M_PATH, v)) return 0;
      scopy(file, M_PATH, v);
      comma = strrchr(file, ',');
      if (comma) *comma = 0;
      e = check_relpath(file, 0);
      if (e) return failk(p, "icon", e);
    } else {
      return failk(p, key, "options are args=, desc= and icon=");
    }
  }
  return 1;
}

static int parse_tgroup(Parser* p, const char* key, char* val)
{
  Manifest* m = p->m;
  MGroup* g;
  char* parts[6];
  char* s;
  int n, i;
  const char* e;
  if (m->ngroups >= M_MAX_GROUPS) return failk(p, key, "too many groups (at most 8)");
  g = &m->groups[m->ngroups];
  memset(g, 0, sizeof(*g));
  n = split_bar(val, parts, 5);
  if (n > 5 || n < 2) return failk(p, key, "expected <title> | .ext .ext* ... [| edit] [| note=<text>]");
  e = check_text(parts[0], 0);
  if (e || !parts[0][0]) return failk(p, key, e ? e : "the title must not be empty");
  if (!set_str(p, key, g->title, M_NAME, parts[0])) return 0;
  for (i = 2; i < n; i++) {
    if (ieq(parts[i], "edit")) g->edit = 1;
    else if (istarts(parts[i], "note=")) {
      char* v = parts[i] + 5;
      trim(v);
      if (check_text(v, 0)) return failk(p, "note", "control characters are not allowed");
      if (!set_str(p, "note", g->note, (int)sizeof(g->note), v)) return 0;
    } else {
      return failk(p, key, "options are edit and note=<text>");
    }
  }
  s = parts[1];
  for (;;) {
    char ext[40];
    int k = 0, def = 0, j;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    if (!*s) break;
    while (*s && *s != ' ' && *s != '\t' && *s != ',' && k < (int)sizeof(ext) - 1) ext[k++] = *s++;
    ext[k] = 0;
    if (k && ext[k - 1] == '*') { def = 1; ext[--k] = 0; }
    if (ext[0] != '.' || k < 2 || k > 15) return failk(p, key, "an extension looks like .txt (add * to tick it by default)");
    for (j = 1; j < k; j++) if (!is_alnum(ext[j]) && ext[j] != '_' && ext[j] != '-')
      return failk(p, key, "extensions may contain only letters, digits, _ and -");
    for (j = 0; j < m->next; j++) if (ieq(m->exts[j].ext, ext)) {
      char buf[100];
      s98_fmt(buf, sizeof(buf), "%s is listed twice", ext);
      return failk(p, key, buf);
    }
    if (m->next >= M_MAX_EXT) return failk(p, key, "too many extensions (at most 48)");
    for (j = 0; ext[j]; j++) if (ext[j] >= 'A' && ext[j] <= 'Z') ext[j] = (char)(ext[j] + 32);
    scopy(m->exts[m->next].ext, 16, ext);
    m->exts[m->next].group = m->ngroups;
    m->exts[m->next].def = def;
    m->next++;
  }
  for (i = 0; i < m->next; i++) if (m->exts[i].group == m->ngroups) break;
  if (i == m->next) return failk(p, key, "the group lists no extensions");
  m->ngroups++;
  return 1;
}

static int do_key(Parser* p, int sec, char* key, char* val)
{
  Manifest* m = p->m;
  int k;
  const char* e = 0;
  char buf[300];
  for (k = 0; k < K_COUNT; k++) if (g_keys[k].sec == sec && ieq(g_keys[k].name, key)) break;
  if (k == K_COUNT) {
    s98_fmt(buf, sizeof(buf), "unknown key \"%.60s\" in [%s]", key, g_secname[sec]);
    return fail(p, buf);
  }
  if (g_keys[k].pack_only && !(p->flags & MP_PACK)) return 1;     // pack-time key in a canonical copy: ignore
  if (!g_keys[k].multi && p->seen[k]) {
    s98_fmt(buf, sizeof(buf), "%s is set twice (first on line %d)", g_keys[k].name, p->seen[k]);
    return fail(p, buf);
  }
  p->seen[k] = p->line;
  key = (char*)g_keys[k].name;
  switch (k) {
  case K_ID:
    e = check_ident(val, 40);
    if (e) return failk(p, key, e);
    return set_str(p, key, m->id, sizeof(m->id), val);
  case K_NAME:
    e = check_filename(val, 0);
    if (e) return failk(p, key, e);
    return set_str(p, key, m->name, M_NAME, val);
  case K_VERSION:
    m->line_version = p->line;
    if (istarts(val, "from:")) {
      char* f = val + 5;
      if (!(p->flags & MP_PACK)) return failk(p, key, "from: is resolved by pack98");
      trim(f);
      e = check_relpath(f, 0);
      if (e) return failk(p, key, e);
      return set_str(p, key, m->version_from, M_PATH, f);
    }
    if (!ver_parse(val, &m->ver_ms, &m->ver_ls))
      return failk(p, key, "expected 1 to 4 numbers 0..65535 separated by dots (1.0, 1.0.2) or from:<file.exe>");
    ver_format(m->ver_ms, m->ver_ls, m->version);
    return 1;
  case K_PUBLISHER: case K_RUNARGS: case K_WCLASS: case K_WTITLE: case K_DATANAME: case K_URL:
    e = check_text(val, 0);
    if (e) return failk(p, key, e);
    if (k == K_PUBLISHER) return set_str(p, key, m->publisher, M_NAME, val);
    if (k == K_RUNARGS) return set_str(p, key, m->run_args, M_PATH, val);
    if (k == K_WCLASS) return set_str(p, key, m->window_class, M_NAME, val);
    if (k == K_WTITLE) return set_str(p, key, m->window_title, (int)sizeof(m->window_title), val);
    if (k == K_DATANAME) return set_str(p, key, m->data_name, (int)sizeof(m->data_name), val);
    return set_str(p, key, m->url, M_PATH, val);
  case K_MAINEXE: case K_README: case K_LICENSE:
    e = check_relpath(val, 0);
    if (e) return failk(p, key, e);
    if (k == K_MAINEXE) {
      int n = (int)strlen(val);
      m->line_mainexe = p->line;
      if (n < 5 || !ieq(val + n - 4, ".exe")) return failk(p, key, "must be an .exe file");
      return set_str(p, key, m->mainexe, M_PATH, val);
    }
    if (k == K_README) { m->line_readme = p->line; return set_str(p, key, m->readme, M_PATH, val); }
    m->line_license = p->line;
    return set_str(p, key, m->license, M_PATH, val);
  case K_FOLDER:
    e = check_folder(val);
    if (e) return failk(p, key, e);
    return set_str(p, key, m->folder, M_PATH, val);
  case K_DESC: {
    char* s = val;
    char* d = val;
    while (*s) {                 // \n means a line break
      if (s[0] == '\\' && s[1] == 'n') { *d++ = '\n'; s += 2; }
      else *d++ = *s++;
    }
    *d = 0;
    e = check_text(val, 1);
    if (e) return failk(p, key, e);
    if (strlen(val) > 400) return failk(p, key, "too long for the Welcome page (at most 400 characters)");
    return set_str(p, key, m->description, M_TEXT, val);
  }
  case K_AGREE: e = check_bool(val, &m->license_agree); break;
  case K_RUN: e = check_bool(val, &m->run); break;
  case K_UNINST: {
    int n = (int)strlen(val);
    e = check_filename(val, 0);
    if (!e && (n < 5 || n > 12 || !ieq(val + n - 4, ".exe"))) e = "must be an 8.3 .exe name like uninst.exe";
    if (!e && strchr(val, ' ')) e = "must not contain spaces";
    if (e) return failk(p, key, e);
    return set_str(p, key, m->uninstaller, (int)sizeof(m->uninstaller), val);
  }
  case K_LOGNAME:
    e = check_ident(val, 8);
    if (e) return failk(p, key, e[0] == 't' ? "1 to 8 characters (the log is %TEMP%\\<LogName>.LOG)" : e);
    return set_str(p, key, m->logname, (int)sizeof(m->logname), val);
  case K_SOURCE: return parse_rule(p, R_SOURCE, key, val);
  case K_FILE: return parse_rule(p, R_FILE, key, val);
  case K_KEEP: return parse_rule(p, R_KEEP, key, val);
  case K_EXCLUDE: return parse_rule(p, R_EXCLUDE, key, val);
  case K_USERDATA: case K_GENERATED: {
    int* n = k == K_USERDATA ? &m->nuserdata : &m->ngenerated;
    char (*arr)[M_PATH] = k == K_USERDATA ? m->userdata : m->generated;
    if (*n >= M_MAX_PAT) return failk(p, key, "too many patterns (at most 32)");
    e = check_datapat(val);
    if (e) return failk(p, key, e);
    if (!set_str(p, key, arr[*n], M_PATH, val)) return 0;
    (*n)++;
    return 1;
  }
  case K_STARTMENU: e = check_opt3(val, &m->startmenu); break;
  case K_DESKTOP: e = check_opt3(val, &m->desktop); break;
  case K_GROUP:
    e = check_filename(val, 1);
    if (e) return failk(p, key, e);
    m->group_set = 1;
    return set_str(p, key, m->group, M_NAME, val);
  case K_LINK: case K_DLINK: {
    int* n = k == K_LINK ? &m->nlinks : &m->ndlinks;
    MLink* arr = k == K_LINK ? m->links : m->dlinks;
    if (*n >= M_MAX_LINKS) return failk(p, key, "too many shortcuts (at most 12)");
    if (!parse_link(p, key, val, &arr[*n])) return 0;
    (*n)++;
    return 1;
  }
  case K_DX:
    if (!(val[0] >= '1' && val[0] <= '9' && !val[1])) return failk(p, key, "expected a major version 1..9 (e.g. 6)");
    m->req_dx = val[0] - '0';
    return 1;
  case K_WIN:
    if (ieq(val, "95")) m->req_win = WIN_95;
    else if (ieq(val, "98")) m->req_win = WIN_98;
    else if (ieq(val, "98SE")) m->req_win = WIN_98SE;
    else if (ieq(val, "ME")) m->req_win = WIN_ME;
    else return failk(p, key, "expected 95, 98, 98SE or ME");
    return 1;
  case K_TGROUP: return parse_tgroup(p, key, val);
  case K_INTRO:
    e = check_text(val, 0);
    if (e) return failk(p, key, e);
    return set_str(p, key, m->types_intro, (int)sizeof(m->types_intro), val);
  case K_LEGACY:
    if (val[0]) {
      int i;
      for (i = 0; val[i]; i++) if (!is_alnum(val[i]) && val[i] != '.' && val[i] != '_' && val[i] != '-')
        return failk(p, key, "a ProgID contains only letters, digits, . _ and -");
    }
    return set_str(p, key, m->legacy_progid, M_NAME, val);
  case K_TICON: {
    char file[M_PATH];
    char* comma;
    m->line_icon = p->line;
    if (!set_str(p, key, m->type_icon, M_PATH, val)) return 0;
    scopy(file, M_PATH, val);
    comma = strrchr(file, ',');
    if (comma) *comma = 0;
    e = check_relpath(file, 0);
    if (e) return failk(p, key, e);
    return 1;
  }
  case K_EDITVERB: case K_CTEXT:
    e = check_text(val, 0);
    if (!e && !val[0]) e = "must not be empty";
    if (e) return failk(p, key, e);
    return set_str(p, key, k == K_EDITVERB ? m->edit_verb : m->ctx_text, M_NAME, val);
  case K_CDEFAULT: e = check_bool(val, &m->ctx_default); break;
  case K_OUTFILE: case K_STUB:
    e = check_text(val, 0);
    if (!e && !val[0]) e = "must not be empty";
    if (e) return failk(p, key, e);
    return set_str(p, key, k == K_OUTFILE ? m->outfile : m->stub, M_PATH, val);
  }
  if (e) return failk(p, key, e);
  return 1;
}

static void set_link(MLink* l, const char* name, const char* target)
{
  memset(l, 0, sizeof(*l));
  scopy(l->name, M_NAME, name);
  scopy(l->target, M_PATH, target);
}

void manifest_finish(Manifest* m)
{
  if (!m->folder[0]) s98_fmt(m->folder, M_PATH, "%%ProgramFiles%%\\%s", m->name);
  if (!m->uninstaller[0]) scopy(m->uninstaller, (int)sizeof(m->uninstaller), "uninst.exe");
  if (!m->data_name[0]) scopy(m->data_name, (int)sizeof(m->data_name), "your settings and saved data");
  if (!m->logname[0]) m_log_name(m, m->logname);
  if (m->startmenu != OPT_OFF && m->nlinks == 0 && m->mainexe[0]) {
    set_link(&m->links[0], m->name, m->mainexe);
    m->nlinks = 1;
  }
  if (!m->group_set) {
    scopy(m->group, M_NAME, m->nlinks > 1 ? m->name : "");
    m->group_set = 1;
  }
  if (m->desktop != OPT_OFF && m->ndlinks == 0 && m->mainexe[0]) {
    if (m->nlinks) m->dlinks[0] = m->links[0];
    else set_link(&m->dlinks[0], m->name, m->mainexe);
    m->ndlinks = 1;
  }
  if (m->has_types) {
    if (!m->types_intro[0])
      s98_fmt(m->types_intro, (int)sizeof(m->types_intro),
              "Ticked types open in %s when you double-click them. Unticked types keep their program.", m->name);
    if (!m->edit_verb[0]) s98_fmt(m->edit_verb, M_NAME, "&Edit with %s", m->name);
    if (!m->type_icon[0]) s98_fmt(m->type_icon, M_PATH, "%s,0", m->mainexe);
  }
  if (m->has_context && !m->ctx_text[0]) s98_fmt(m->ctx_text, M_NAME, "Open with &%s", m->name);
}

static int check_links(Parser* p, const MLink* l, int n, const char* what)
{
  int i, j;
  for (i = 0; i < n; i++)
    for (j = i + 1; j < n; j++)
      if (ieq(l[i].name, l[j].name)) {
        char buf[200];
        p->line = l[j].line;
        s98_fmt(buf, sizeof(buf), "two %s shortcuts are called \"%.80s\"", what, l[j].name);
        return fail(p, buf);
      }
  return 1;
}

int manifest_parse(const char* text, u32 len, Manifest* m, int flags, char* err, int errcap)
{
  Parser p;
  static char conv[65536 * 2];
  char line[2048];
  u32 pos = 0;
  int sec = S_NONE, i;
  memset(m, 0, sizeof(*m));
  memset(&p, 0, sizeof(p));
  p.m = m;
  p.flags = flags;
  p.err = err;
  p.errcap = errcap;
  err[0] = 0;
  m->license_agree = 1;
  m->run = 1;
  m->startmenu = OPT_YES;
  m->desktop = OPT_NO;
  m->ctx_default = 1;
  if (len >= 3 && (u8)text[0] == 0xEF && (u8)text[1] == 0xBB && (u8)text[2] == 0xBF) { text += 3; len -= 3; }
  if (len > sizeof(conv) / 2) return fail(&p, "the manifest is too large (at most 64 KB)");
  // a manifest from pack98's command line may be UTF-8: text values become Windows-1252 (the stub's ANSI UI).
  // Text that isn't valid UTF-8 is taken as Windows-1252 already (Notepad on Win98).
  if (flags & MP_PACK) {
    u32 i2 = 0, o = 0, cp;
    int utf8 = 1, nonascii = 0, ln = 1;
    while (i2 < len) {
      int l = utf8_seq((const u8*)text + i2, len - i2, &cp);
      if (!l) { utf8 = 0; break; }
      if (l > 1) nonascii = 1;
      i2 += (u32)l;
    }
    if (utf8 && nonascii) {
      for (i2 = 0; i2 < len;) {
        int l = utf8_seq((const u8*)text + i2, len - i2, &cp), b = cp1252_from_ucs(cp);
        if (cp == '\n') ln++;
        if (b < 0) {
          p.line = ln;
          return fail(&p, "a character has no Windows-1252 equivalent (Win98's ANSI code page)");
        }
        conv[o++] = (char)b;
        i2 += (u32)l;
      }
      text = conv;
      len = o;
    }
  }
  while (pos < len) {
    u32 e = pos, n;
    char* eq;
    while (e < len && text[e] != '\n') e++;
    n = e - pos;
    p.line++;
    if (n >= sizeof(line)) return fail(&p, "line is too long (at most 2000 characters)");
    if (memchr(text + pos, 0, n)) return fail(&p, "NUL byte in the text");
    memcpy(line, text + pos, n);
    line[n] = 0;
    pos = e < len ? e + 1 : e;
    if (n && line[n - 1] == '\r') line[n - 1] = 0;
    if (strchr(line, '\r')) return fail(&p, "stray carriage return");
    trim(line);
    if (!line[0] || line[0] == ';' || line[0] == '#') continue;
    if (line[0] == '[') {
      int k;
      char* close = strchr(line, ']');
      if (!close || close[1]) return fail(&p, "a section header looks like [Product]");
      *close = 0;
      for (k = 1; k < S_SKIP; k++) if (ieq(line + 1, g_secname[k])) break;
      if (k == S_SKIP) {
        if ((flags & MP_RECORD) && istarts(line + 1, "Installed")) { sec = S_SKIP; continue; }
        {
          char buf[200];
          s98_fmt(buf, sizeof(buf), "unknown section [%.60s] (Product, Files, Shortcuts, Requires, Types, ContextMenu, "
                  "Pack)", line + 1);
          return fail(&p, buf);
        }
      }
      if (k == S_PACK && !(flags & MP_PACK)) { sec = S_SKIP; continue; }
      if (p.secseen[k]) {
        char buf[100];
        s98_fmt(buf, sizeof(buf), "[%s] appears twice (first on line %d)", g_secname[k], p.secseen[k]);
        return fail(&p, buf);
      }
      p.secseen[k] = p.line;
      if (k == S_TYPES) m->has_types = 1;
      if (k == S_CONTEXT) m->has_context = 1;
      sec = k;
      continue;
    }
    if (sec == S_SKIP) continue;
    if (sec == S_NONE) return fail(&p, "a key before the first [section]");
    eq = strchr(line, '=');
    if (!eq) return fail(&p, "expected Key=Value");
    *eq = 0;
    trim(line);
    trim(eq + 1);
    if (!line[0]) return fail(&p, "the key is missing before =");
    if (!do_key(&p, sec, line, eq + 1)) return 0;
  }
  p.line = 0;
  if (!p.secseen[S_PRODUCT]) return fail(&p, "[Product] is missing");
  if (!m->id[0]) return fail(&p, "[Product] Id= is required");
  if (!m->name[0]) return fail(&p, "[Product] Name= is required");
  if (!m->version[0] && !m->version_from[0]) return fail(&p, "[Product] Version= is required");
  if (!m->mainexe[0]) return fail(&p, "[Product] MainExe= is required");
  if (flags & MP_PACK) {
    int files = 0;
    for (i = 0; i < m->nrules; i++) if (m->rules[i].kind == R_FILE || m->rules[i].kind == R_KEEP) files++;
    if (!files) return fail(&p, "[Files] needs at least one File= line");
  }
  if (m->startmenu == OPT_OFF && m->nlinks) { p.line = m->links[0].line; return fail(&p, "Link= with StartMenu=off"); }
  if (m->desktop == OPT_OFF && m->ndlinks) { p.line = m->dlinks[0].line; return fail(&p, "DesktopLink= with Desktop=off"); }
  if (m->has_types && !m->ngroups) { p.line = p.secseen[S_TYPES]; return fail(&p, "[Types] needs at least one Group="); }
  if (!check_links(&p, m->links, m->nlinks, "Start menu") || !check_links(&p, m->dlinks, m->ndlinks, "desktop")) return 0;
  p.line = 0;
  if (m->license_agree && !m->license[0]) m->license_agree = 0;
  manifest_finish(m);
  if (m->has_types) {
    TypesBox boxes[M_MAX_GROUPS];
    if (!types_layout(m, boxes)) {
      p.line = p.secseen[S_TYPES];
      return fail(&p, "the file types don't fit on the File types page: use fewer groups or extensions, or shorter notes");
    }
  }
  return 1;
}

// ---- canonical writer ----

struct Writer {
  char* out;
  u32 cap, n;
  int full;
};

static void w_raw(Writer* w, const char* s)
{
  u32 l = (u32)strlen(s);
  if (w->n + l + 1 > w->cap) { w->full = 1; return; }
  memcpy(w->out + w->n, s, l);
  w->n += l;
  w->out[w->n] = 0;
}

static void w_kv(Writer* w, const char* key, const char* val)
{
  w_raw(w, key);
  w_raw(w, "=");
  w_raw(w, val);
  w_raw(w, "\r\n");
}

static void w_kv_int(Writer* w, const char* key, int v)
{
  char buf[16];
  s98_fmt(buf, sizeof(buf), "%d", v);
  w_kv(w, key, buf);
}

static void w_opt3(Writer* w, const char* key, int v)
{
  w_kv(w, key, v == OPT_OFF ? "off" : v ? "1" : "0");
}

static void w_link(Writer* w, const char* key, const MLink* l)
{
  w_raw(w, key);
  w_raw(w, "=");
  w_raw(w, l->name);
  w_raw(w, " | ");
  w_raw(w, l->target);
  if (l->args[0]) { w_raw(w, " | args="); w_raw(w, l->args); }
  if (l->desc[0]) { w_raw(w, " | desc="); w_raw(w, l->desc); }
  if (l->icon[0]) { w_raw(w, " | icon="); w_raw(w, l->icon); }
  w_raw(w, "\r\n");
}

u32 manifest_write(const Manifest* m, char* out, u32 cap)
{
  Writer w;
  int i, g;
  char buf[M_TEXT * 2];
  w.out = out;
  w.cap = cap;
  w.n = 0;
  w.full = 0;
  if (cap) out[0] = 0;
  w_raw(&w, "; Setup98 canonical manifest\r\n[Product]\r\n");
  w_kv(&w, "Id", m->id);
  w_kv(&w, "Name", m->name);
  w_kv(&w, "Version", m->version);
  if (m->publisher[0]) w_kv(&w, "Publisher", m->publisher);
  w_kv(&w, "MainExe", m->mainexe);
  w_kv(&w, "Folder", m->folder);
  if (m->description[0]) {
    const char* s = m->description;
    char* d = buf;
    while (*s && d < buf + sizeof(buf) - 3) {
      if (*s == '\n') { *d++ = '\\'; *d++ = 'n'; s++; }
      else *d++ = *s++;
    }
    *d = 0;
    w_kv(&w, "Description", buf);
  }
  if (m->readme[0]) w_kv(&w, "Readme", m->readme);
  if (m->license[0]) w_kv(&w, "License", m->license);
  w_kv_int(&w, "LicenseAgree", m->license_agree);
  w_kv_int(&w, "Run", m->run);
  if (m->run_args[0]) w_kv(&w, "RunArgs", m->run_args);
  if (m->window_class[0]) w_kv(&w, "WindowClass", m->window_class);
  if (m->window_title[0]) w_kv(&w, "WindowTitle", m->window_title);
  w_kv(&w, "Uninstaller", m->uninstaller);
  w_kv(&w, "DataName", m->data_name);
  w_kv(&w, "LogName", m->logname);
  if (m->url[0]) w_kv(&w, "URL", m->url);
  if (m->nuserdata || m->ngenerated) {
    w_raw(&w, "[Files]\r\n");
    for (i = 0; i < m->nuserdata; i++) w_kv(&w, "UserData", m->userdata[i]);
    for (i = 0; i < m->ngenerated; i++) w_kv(&w, "Generated", m->generated[i]);
  }
  w_raw(&w, "[Shortcuts]\r\n");
  w_opt3(&w, "StartMenu", m->startmenu);
  w_opt3(&w, "Desktop", m->desktop);
  w_kv(&w, "Group", m->group);
  for (i = 0; i < m->nlinks; i++) w_link(&w, "Link", &m->links[i]);
  for (i = 0; i < m->ndlinks; i++) w_link(&w, "DesktopLink", &m->dlinks[i]);
  if (m->req_dx || m->req_win) {
    static const char* const wn[] = { "", "95", "98", "98SE", "ME" };
    w_raw(&w, "[Requires]\r\n");
    if (m->req_dx) w_kv_int(&w, "DirectX", m->req_dx);
    if (m->req_win) w_kv(&w, "Windows", wn[m->req_win]);
  }
  if (m->has_types) {
    w_raw(&w, "[Types]\r\n");
    for (g = 0; g < m->ngroups; g++) {
      w_raw(&w, "Group=");
      w_raw(&w, m->groups[g].title);
      w_raw(&w, " |");
      for (i = 0; i < m->next; i++) {
        if (m->exts[i].group != g) continue;
        w_raw(&w, " ");
        w_raw(&w, m->exts[i].ext);
        if (m->exts[i].def) w_raw(&w, "*");
      }
      if (m->groups[g].edit) w_raw(&w, " | edit");
      if (m->groups[g].note[0]) { w_raw(&w, " | note="); w_raw(&w, m->groups[g].note); }
      w_raw(&w, "\r\n");
    }
    w_kv(&w, "Intro", m->types_intro);
    if (m->legacy_progid[0]) w_kv(&w, "LegacyProgId", m->legacy_progid);
    w_kv(&w, "Icon", m->type_icon);
    w_kv(&w, "EditVerb", m->edit_verb);
  }
  if (m->has_context) {
    w_raw(&w, "[ContextMenu]\r\n");
    w_kv(&w, "Text", m->ctx_text);
    w_kv_int(&w, "Default", m->ctx_default);
  }
  return w.full ? 0 : w.n;
}

// ---- File types page layout ----
// Groups flow left to right: a group whose extensions fit in one row (and has no note) shares the row with the next
// such group if both fit; others take the full width. The last box of each row stretches to the right edge.
int types_layout(const Manifest* m, TypesBox* b)
{
  enum { X0 = 7, X1 = 310, Y0 = 30, Y1 = 139, CW = 38 };
  int g, x = X0, y = Y0, rowh = 0, rowfirst = 0;
  for (g = 0; g < m->ngroups; g++) {
    int k = 0, i, single, w, rows;
    int note = m->groups[g].note[0] != 0;
    for (i = 0; i < m->next; i++) if (m->exts[i].group == g) k++;
    single = k * CW + 14;
    if (note || single > X1 - X0) {
      w = X1 - X0;
      if (x > X0) { y += rowh + 4; rowh = 0; x = X0; }
    } else {
      w = single;
      if (x > X0 && x + w > X1) { y += rowh + 4; rowh = 0; x = X0; }
    }
    if (x == X0) {
      if (g > rowfirst) b[g - 1].w = X1 - b[g - 1].x;       // stretch the previous row's last box
      rowfirst = g;
    }
    b[g].x = x;
    b[g].y = y;
    b[g].w = w;
    b[g].cols = (w - 10) / CW;
    if (b[g].cols < 1) b[g].cols = 1;
    rows = (k + b[g].cols - 1) / b[g].cols;
    b[g].top = y + 11 + (note ? 10 : 0);
    b[g].h = b[g].top - y + rows * 12 + 2;
    if (b[g].h > rowh) rowh = b[g].h;
    x += w + 5;
    if (note || w == X1 - X0) x = X1 + 1;                    // nothing goes beside a full-width box
  }
  if (m->ngroups) b[m->ngroups - 1].w = X1 - b[m->ngroups - 1].x;
  for (g = 0; g < m->ngroups; g++) {                         // same height for boxes in one row
    int j;
    for (j = 0; j < m->ngroups; j++) if (b[j].y == b[g].y && b[j].h > b[g].h) b[g].h = b[j].h;
  }
  return y + rowh <= Y1;
}
