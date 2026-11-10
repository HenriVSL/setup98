// assoc.cpp - Setup98 file associations and "Open with <Name>", ported from Pica's setup (same registry layout, so a
// Setup98 build with Id=Pica takes over a Pica 1.0 install). Rule: never change anything we can't put back exactly.
//
// HKLM\Software\<Id>\Previous\Open\<.ext>     what HKCR\.ext held before: "=<ProgID>", "!" (key without default),
//                                              "-" (no key: we created it)
// HKLM\Software\<Id>\Previous\Edit\<ProgID>   what <ProgID>\shell\edit\command held: "=<cmd>", "!" no value, "-" no
//                                              edit verb (ours)
// HKLM\Software\<Id>\Previous\Created         HKCR keys Setup created: deleted again once empty
// HKCR\<Id>.<ext>                              one ProgID per ticked type: name and non-open verbs copied from the
//                                              previous ProgID, open = us, DefaultIcon = us
// Restores happen only while the entry still points at us.
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "s98.h"

static void key_prev(char* out, const char* which)
{
  key_app(out);
  s_cat(out, 300, "\\Previous\\");
  s_cat(out, 300, which);
}

void our_command(const char* dir, char* out, int cap)
{
  char exe[MAX_PATH];
  p_join(dir, g_m.mainexe, exe, sizeof(exe));
  s_copy(out, cap, "\"");
  s_cat(out, cap, exe);
  s_cat(out, cap, "\" \"%1\"");
}

static void encode_prev(int st, const char* cur, char* rec, int cap)
{
  if (st < 0) s_copy(rec, cap, "-");
  else if (st == 0) s_copy(rec, cap, "!");
  else { s_copy(rec, cap, "="); s_cat(rec, cap, cur); }
}

static void restore_default(HKEY root, const char* key, const char* rec)
{
  if (rec[0] == '=') reg_set(root, key, 0, rec + 1);
  else reg_del_value(root, key, "");
}

// records a key we created under HKCR, so uninstall can delete it again once it is empty
static void record_created(const char* key)
{
  HKEY h;
  DWORD one = 1;
  char name[300], kp[300];
  s_copy(name, sizeof(name), "HKCR\\");
  s_cat(name, sizeof(name), key);
  key_prev(kp, "Created");
  if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, kp, 0, 0, 0, KEY_WRITE, 0, &h, 0) != ERROR_SUCCESS) return;
  RegSetValueExA(h, name, 0, REG_DWORD, (const BYTE*)&one, sizeof(one));
  RegCloseKey(h);
}

// creates every missing part of key; under HKCR with track=1 each new part is recorded (record_created)
static int reg_ensure(HKEY root, const char* key, int track)
{
  char buf[300];
  int i, n;
  HKEY h;
  s_copy(buf, sizeof(buf), key);
  n = lstrlenA(buf);
  for (i = 1; i <= n; i++) {
    if (buf[i] == '\\' || buf[i] == 0) {
      char c = buf[i];
      buf[i] = 0;
      if (!reg_key_exists(root, buf)) {
        if (RegCreateKeyExA(root, buf, 0, 0, 0, KEY_WRITE, 0, &h, 0) != ERROR_SUCCESS) {
          slog("error: cannot create %s\\%s", root_name(root), buf);
          return 0;
        }
        RegCloseKey(h);
        if (track && root == HKEY_CLASSES_ROOT) record_created(buf);
      }
      buf[i] = c;
    }
  }
  return 1;
}

static void ext_progid(const char* ext, char* out, int cap)
{
  s_copy(out, cap, g_m.id);
  s_cat(out, cap, ".");
  s_cat(out, cap, ext[0] == '.' ? ext + 1 : ext);
}

static int is_ours(const Manifest* m, const char* ext, const char* cur)
{
  char pid[64];
  ext_progid(ext, pid, sizeof(pid));
  return lstrcmpiA(cur, pid) == 0 || (m->legacy_progid[0] && lstrcmpiA(cur, m->legacy_progid) == 0);
}

static int starts_ours(const char* progid)
{
  char pre[64];
  s_copy(pre, sizeof(pre), g_m.id);
  s_cat(pre, sizeof(pre), ".");
  return starts_i(progid, pre);
}

// copies key src (values and subkeys) to dst under HKCR; dst is ours, so nothing is tracked
static void reg_copy_tree(const char* src, const char* dst, int depth)
{
  HKEY hs, hd;
  DWORD i, nlen, dlen, type;
  char name[260], sub[600], dsub[600];
  BYTE data[2048];
  if (depth > 8 || RegOpenKeyExA(HKEY_CLASSES_ROOT, src, 0, KEY_READ, &hs) != ERROR_SUCCESS) return;
  if (RegCreateKeyExA(HKEY_CLASSES_ROOT, dst, 0, 0, 0, KEY_WRITE, 0, &hd, 0) != ERROR_SUCCESS) { RegCloseKey(hs); return; }
  for (i = 0;; i++) {
    nlen = sizeof(name);
    dlen = sizeof(data);
    if (RegEnumValueA(hs, i, name, &nlen, 0, &type, data, &dlen) != ERROR_SUCCESS) break;
    RegSetValueExA(hd, name, 0, type, data, dlen);
  }
  RegCloseKey(hd);
  for (i = 0;; i++) {
    nlen = sizeof(name);
    if (RegEnumKeyExA(hs, i, name, &nlen, 0, 0, 0, 0) != ERROR_SUCCESS) break;
    if (lstrlenA(src) + nlen + 2 >= sizeof(sub) || lstrlenA(dst) + nlen + 2 >= sizeof(dsub)) continue;
    s_copy(sub, sizeof(sub), src); s_cat(sub, sizeof(sub), "\\"); s_cat(sub, sizeof(sub), name);
    s_copy(dsub, sizeof(dsub), dst); s_cat(dsub, sizeof(dsub), "\\"); s_cat(dsub, sizeof(dsub), name);
    reg_copy_tree(sub, dsub, depth + 1);
  }
  RegCloseKey(hs);
}

// (re)builds HKCR\<Id>.<ext> from the saved previous value rec
static int build_progid(const Manifest* m, const char* ext, const char* rec, const char* dir, const char* cmd)
{
  char pid[64], key[400], src[260], desc[300], icon[MAX_PATH + 8], verbs[300], name[260];
  int have_src = 0;
  HKEY h;
  DWORD i, nlen;
  ext_progid(ext, pid, sizeof(pid));
  reg_del_tree(HKEY_CLASSES_ROOT, pid);
  if (rec[0] == '=' && rec[1] && !starts_ours(rec + 1) && reg_key_exists(HKEY_CLASSES_ROOT, rec + 1)) {
    s_copy(src, sizeof(src), rec + 1);
    have_src = 1;
  }
  if (!have_src || reg_get(HKEY_CLASSES_ROOT, src, 0, desc, sizeof(desc)) != 1 || !desc[0]) {
    s_copy(desc, sizeof(desc), ext[0] == '.' ? ext + 1 : ext);
    CharUpperA(desc);
    s_cat(desc, sizeof(desc), " File");
  }
  p_join(dir, m->type_icon, icon, sizeof(icon));
  if (!strchr(m->type_icon, ',')) s_cat(icon, sizeof(icon), ",0");
  s_copy(key, sizeof(key), pid);
  s_cat(key, sizeof(key), "\\shell\\open\\command");
  if (!reg_ensure(HKEY_CLASSES_ROOT, key, 0) || !reg_set(HKEY_CLASSES_ROOT, key, 0, cmd)) return 0;
  s_copy(key, sizeof(key), pid);
  s_cat(key, sizeof(key), "\\DefaultIcon");
  if (!reg_set(HKEY_CLASSES_ROOT, pid, 0, desc) || !reg_set(HKEY_CLASSES_ROOT, key, 0, icon)) return 0;
  verbs[0] = 0;
  if (have_src) {
    s_copy(key, sizeof(key), src);
    s_cat(key, sizeof(key), "\\shell");
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
      for (i = 0;; i++) {
        char from[600], to[600];
        nlen = sizeof(name);
        if (RegEnumKeyExA(h, i, name, &nlen, 0, 0, 0, 0) != ERROR_SUCCESS) break;
        if (lstrcmpiA(name, "open") == 0) continue;
        s_copy(from, sizeof(from), key); s_cat(from, sizeof(from), "\\"); s_cat(from, sizeof(from), name);
        s_copy(to, sizeof(to), pid); s_cat(to, sizeof(to), "\\shell\\"); s_cat(to, sizeof(to), name);
        reg_copy_tree(from, to, 0);
        if (verbs[0]) s_cat(verbs, sizeof(verbs), ",");
        s_cat(verbs, sizeof(verbs), name);
      }
      RegCloseKey(h);
    }
  }
  s_copy(key, sizeof(key), pid);
  s_cat(key, sizeof(key), "\\shell");
  reg_set(HKEY_CLASSES_ROOT, key, 0, "open");
  slog("progid: HKCR\\%s \"%s\", verbs open%s%s (from %s)", pid, desc, verbs[0] ? "," : "", verbs,
       have_src ? src : "nothing");
  return 1;
}

static int open_assoc(const Manifest* m, const char* ext, const char* dir, const char* cmd)
{
  char cur[300], rec[310], pid[64], kp[300];
  int st = reg_get(HKEY_CLASSES_ROOT, ext, 0, cur, sizeof(cur));
  key_prev(kp, "Open");
  ext_progid(ext, pid, sizeof(pid));
  if (st == 1 && is_ours(m, ext, cur)) {        // already ours: keep the first saved previous value
    if (reg_get(HKEY_LOCAL_MACHINE, kp, ext, rec, sizeof(rec)) != 1) s_copy(rec, sizeof(rec), "!");
  } else {
    encode_prev(st, cur, rec, sizeof(rec));
    if (!reg_set(HKEY_LOCAL_MACHINE, kp, ext, rec)) return 0;
  }
  if (!build_progid(m, ext, rec, dir, cmd)) return 0;
  if (!(st == 1 && lstrcmpiA(cur, pid) == 0)) {
    if (!reg_ensure(HKEY_CLASSES_ROOT, ext, 1) || !reg_set(HKEY_CLASSES_ROOT, ext, 0, pid)) return 0;
    slog("open: HKCR\\%s %s -> %s", ext, st == 1 && is_ours(m, ext, cur) ? cur : rec, pid);
  }
  return 1;
}

static void open_unassoc(const Manifest* m, const char* ext)
{
  char cur[300], rec[310], pid[64], kp[300];
  int st;
  key_prev(kp, "Open");
  if (reg_get(HKEY_LOCAL_MACHINE, kp, ext, rec, sizeof(rec)) != 1) return;
  st = reg_get(HKEY_CLASSES_ROOT, ext, 0, cur, sizeof(cur));
  if (st == 1 && is_ours(m, ext, cur)) {
    restore_default(HKEY_CLASSES_ROOT, ext, rec);
    slog("open: HKCR\\%s restored %s", ext, rec);
  } else {
    slog("open: HKCR\\%s now \"%s\", not ours: left alone", ext, cur);
  }
  ext_progid(ext, pid, sizeof(pid));
  reg_del_tree(HKEY_CLASSES_ROOT, pid);
  reg_del_value(HKEY_LOCAL_MACHINE, kp, ext);
}

// the ProgID that runs a script type, or 0 when nothing handles it (then we may open it)
static int script_handler(const Manifest* m, const char* ext, char* progid, int cap)
{
  if (reg_get(HKEY_CLASSES_ROOT, ext, 0, progid, cap) != 1 || !progid[0]) return 0;
  if (is_ours(m, ext, progid)) return 0;
  return reg_key_exists(HKEY_CLASSES_ROOT, progid);
}

static int edit_assoc(const Manifest* m, const char* progid, const char* cmd)
{
  char verb[300], ckey[300], cur[600], rec[610], kp[300];
  int st, verb_exists;
  key_prev(kp, "Edit");
  s_copy(verb, sizeof(verb), progid);
  s_cat(verb, sizeof(verb), "\\shell\\edit");
  s_copy(ckey, sizeof(ckey), verb);
  s_cat(ckey, sizeof(ckey), "\\command");
  st = reg_get(HKEY_CLASSES_ROOT, ckey, 0, cur, sizeof(cur));
  if (st == 1 && lstrcmpiA(cur, cmd) == 0) return 1;
  verb_exists = reg_key_exists(HKEY_CLASSES_ROOT, verb);
  if (!verb_exists) s_copy(rec, sizeof(rec), "-");
  else encode_prev(st < 0 ? 0 : st, cur, rec, sizeof(rec));
  if (!reg_set(HKEY_LOCAL_MACHINE, kp, progid, rec)) return 0;
  if (!verb_exists) {
    char shell[300];
    s_copy(shell, sizeof(shell), progid);
    s_cat(shell, sizeof(shell), "\\shell");
    if (!reg_ensure(HKEY_CLASSES_ROOT, shell, 1) || !reg_ensure(HKEY_CLASSES_ROOT, ckey, 0)) return 0;
    reg_set(HKEY_CLASSES_ROOT, verb, 0, m->edit_verb);
  } else if (!reg_ensure(HKEY_CLASSES_ROOT, ckey, 1)) {
    return 0;
  }
  if (!reg_set(HKEY_CLASSES_ROOT, ckey, 0, cmd)) return 0;
  slog("edit: HKCR\\%s %s -> %s", ckey, rec, cmd);
  return 1;
}

static void edit_restore(const char* progid, const char* cmd)
{
  char verb[300], ckey[300], cur[600], rec[610], kp[300];
  key_prev(kp, "Edit");
  if (reg_get(HKEY_LOCAL_MACHINE, kp, progid, rec, sizeof(rec)) != 1) return;
  s_copy(verb, sizeof(verb), progid);
  s_cat(verb, sizeof(verb), "\\shell\\edit");
  s_copy(ckey, sizeof(ckey), verb);
  s_cat(ckey, sizeof(ckey), "\\command");
  if (reg_get(HKEY_CLASSES_ROOT, ckey, 0, cur, sizeof(cur)) == 1 && lstrcmpiA(cur, cmd) == 0) {
    if (rec[0] == '-') reg_del_tree(HKEY_CLASSES_ROOT, verb);
    else restore_default(HKEY_CLASSES_ROOT, ckey, rec);
    slog("edit: HKCR\\%s restored %s", ckey, rec);
  } else {
    slog("edit: HKCR\\%s now \"%s\", not ours: left alone", ckey, cur);
  }
  reg_del_value(HKEY_LOCAL_MACHINE, kp, progid);
}

static int in_list(char list[][260], int n, const char* s)
{
  int i;
  for (i = 0; i < n; i++) if (lstrcmpiA(list[i], s) == 0) return 1;
  return 0;
}

int assoc_apply(const Manifest* m, const char* dir, const int* types)
{
  static char want_open[M_MAX_EXT][260], want_edit[M_MAX_EXT][260], recs[128][260];
  int nopen = 0, nedit = 0, nrec, i, ok = 1;
  char cmd[600], progid[260], kp[300];
  our_command(dir, cmd, sizeof(cmd));
  for (i = 0; i < m->next; i++) {
    int edit = m->groups[m->exts[i].group].edit;
    if (!types[i]) continue;
    if (edit && script_handler(m, m->exts[i].ext, progid, sizeof(progid))) {
      if (!in_list(want_edit, nedit, progid)) s_copy(want_edit[nedit++], 260, progid);
    } else {
      if (edit) slog("%s has no program that runs it: %s opens it", m->exts[i].ext, m->name);
      s_copy(want_open[nopen++], 260, m->exts[i].ext);
    }
  }
  key_prev(kp, "Open");
  nrec = reg_value_names(HKEY_LOCAL_MACHINE, kp, recs, 128);
  for (i = 0; i < nrec; i++) if (!in_list(want_open, nopen, recs[i])) open_unassoc(m, recs[i]);
  for (i = 0; i < nopen; i++) if (!open_assoc(m, want_open[i], dir, cmd)) ok = 0;
  key_prev(kp, "Edit");
  nrec = reg_value_names(HKEY_LOCAL_MACHINE, kp, recs, 128);
  for (i = 0; i < nrec; i++) if (!in_list(want_edit, nedit, recs[i])) edit_restore(recs[i], cmd);
  for (i = 0; i < nedit; i++) if (!edit_assoc(m, want_edit[i], cmd)) ok = 0;
  return ok;
}

// deletes the HKCR keys we created once they are empty again (deepest first)
void created_cleanup()
{
  static char recs[128][260];
  char kp[300];
  int n, k, j;
  key_prev(kp, "Created");
  n = reg_value_names(HKEY_LOCAL_MACHINE, kp, recs, 128);
  for (k = 0; k < n; k++) {
    int best = -1, bl = -1;
    const char* key;
    for (j = 0; j < n; j++) if (recs[j][0] && lstrlenA(recs[j]) > bl) { best = j; bl = lstrlenA(recs[j]); }
    if (best < 0) break;
    key = recs[best] + 5;     // after "HKCR\"
    if (!reg_key_exists(HKEY_CLASSES_ROOT, key) || reg_del_if_empty(HKEY_CLASSES_ROOT, key))
      reg_del_value(HKEY_LOCAL_MACHINE, kp, recs[best]);
    recs[best][0] = 0;
  }
}

int context_set(const Manifest* m, int want, const char* cmd)
{
  char key[300], ckey[300];
  s_copy(key, sizeof(key), "*\\shell\\");
  s_cat(key, sizeof(key), g_m.id);
  s_copy(ckey, sizeof(ckey), key);
  s_cat(ckey, sizeof(ckey), "\\command");
  if (!want) {
    reg_del_tree(HKEY_CLASSES_ROOT, key);
    return 1;
  }
  if (!reg_ensure(HKEY_CLASSES_ROOT, "*\\shell", 1) || !reg_ensure(HKEY_CLASSES_ROOT, ckey, 0)) return 0;
  slog("context menu: HKCR\\%s", key);
  return reg_set(HKEY_CLASSES_ROOT, key, 0, m->ctx_text) && reg_set(HKEY_CLASSES_ROOT, ckey, 0, cmd);
}

void legacy_cleanup(const Manifest* m)
{
  if (m->legacy_progid[0]) reg_del_tree(HKEY_CLASSES_ROOT, m->legacy_progid);
}
