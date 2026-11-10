// mantest.cpp - Mac tests of the manifest parser, the canonical writer, path patterns, versions and the File types
// page layout:  mantest <manifest.ini>...
// Each manifest starts with "; expect: ok" or "; expect: <text the error message must contain>". Valid manifests
// must also survive write -> parse -> write unchanged (the stub reads exactly what pack98 writes).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/manifest.h"
#include "../src/wild.h"

static int g_fail = 0, g_checks = 0;

static void check(int ok, const char* what)
{
  g_checks++;
  if (!ok) { g_fail++; printf("FAIL: %s\n", what); }
}

static char* load(const char* path, unsigned int* n)
{
  FILE* f = fopen(path, "rb");
  char* p;
  long sz;
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  p = (char*)malloc((size_t)sz + 1);
  if (fread(p, 1, (size_t)sz, f) != (size_t)sz) sz = 0;
  p[sz] = 0;
  fclose(f);
  *n = (unsigned int)sz;
  return p;
}

static void test_file(const char* path)
{
  static Manifest m, m2;
  static char c1[65536], c2[65536];
  char err[600], expect[300], msgbuf[900];
  unsigned int n;
  char* t = load(path, &n);
  int ok;
  if (!t) { printf("cannot read %s\n", path); g_fail++; return; }
  if (sscanf(t, "; expect: %299[^\r\n]", expect) != 1) { printf("%s: no expect line\n", path); g_fail++; free(t); return; }
  ok = manifest_parse(t, n, &m, MP_PACK, err, sizeof(err));
  if (!strcmp(expect, "ok")) {
    snprintf(msgbuf, sizeof(msgbuf), "%s should parse: %s", path, err);
    check(ok, msgbuf);
    if (ok && !m.version_from[0]) {
      unsigned int l1 = manifest_write(&m, c1, sizeof(c1)), l2;
      int ok2 = manifest_parse(c1, l1, &m2, 0, err, sizeof(err));
      snprintf(msgbuf, sizeof(msgbuf), "%s: canonical text rejected: %s", path, err);
      check(l1 > 0 && ok2, msgbuf);
      l2 = manifest_write(&m2, c2, sizeof(c2));
      snprintf(msgbuf, sizeof(msgbuf), "%s: canonical round trip differs", path);
      check(l1 == l2 && !memcmp(c1, c2, l1), msgbuf);
    }
  } else {
    snprintf(msgbuf, sizeof(msgbuf), "%s: expected an error containing \"%s\", got %s \"%s\"", path, expect,
             ok ? "success" : "error", err);
    check(!ok && strstr(err, expect) != 0, msgbuf);
  }
  free(t);
}

static void test_wild()
{
  static const struct { const char* p; const char* s; int r; } t[] = {
    { "*.bmp", "a.bmp", 1 }, { "*.bmp", "art\\a.bmp", 0 }, { "art\\*.bmp", "art\\a.BMP", 1 },
    { "art\\*.bmp", "art\\sub\\a.bmp", 0 }, { "art\\**", "art\\sub\\a.bmp", 1 }, { "art\\**", "art2\\a", 0 },
    { "**\\*.log", "a.log", 1 }, { "**\\*.log", "x\\y\\a.log", 1 }, { "**\\*.log", "x\\a.txt", 0 },
    { "**.log", "x\\a.log", 1 }, { "session?.ini", "session1.ini", 1 }, { "session?.ini", "session12.ini", 0 },
    { "?", "\\", 0 }, { "a*b*c", "aXXbYYc", 1 }, { "a*b*c", "aXXbYY", 0 }, { "README.TXT", "readme.txt", 1 },
    { "data/**", "data\\KESRA.MAP", 1 }, { "**", "anything\\at\\all", 1 }, { "*", "a\\b", 0 },
    { "**\\~S98????.TMP", "art\\~S980001.TMP", 1 }, { "**\\~S98????.TMP", "~S98000A.TMP", 1 }
  };
  unsigned int i;
  char buf[200];
  for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
    snprintf(buf, sizeof(buf), "wild_match(\"%s\", \"%s\") != %d", t[i].p, t[i].s, t[i].r);
    check(wild_match(t[i].p, t[i].s) == t[i].r, buf);
  }
}

static void test_versions()
{
  unsigned int ms, ls;
  char out[24];
  check(ver_parse("1.0.2", &ms, &ls) && ms == 0x10000 && ls == 0x20000, "ver_parse 1.0.2");
  ver_format(ms, ls, out);
  check(!strcmp(out, "1.0.2"), "ver_format 1.0.2");
  check(ver_parse("1", &ms, &ls) && ms == 0x10000 && ls == 0, "ver_parse 1");
  ver_format(ms, ls, out);
  check(!strcmp(out, "1.0"), "ver_format 1.0");
  check(ver_parse("65535.1.2.3", &ms, &ls), "ver_parse max");
  check(!ver_parse("65536", &ms, &ls), "ver_parse 65536");
  check(!ver_parse("1..2", &ms, &ls) && !ver_parse("1.2.", &ms, &ls) && !ver_parse("1.2.3.4.5", &ms, &ls) &&
        !ver_parse("v1", &ms, &ls) && !ver_parse("", &ms, &ls), "ver_parse rejects");
  check(ver_compare(0x10000, 0x20000, 0x10000, 0x10000) > 0 && ver_compare(0, 9, 1, 0) < 0, "ver_compare");
}

// Pica's hand-made File types page: the generic layout must reproduce it (same coordinates)
static void test_layout()
{
  static Manifest m;
  TypesBox b[M_MAX_GROUPS];
  char err[300];
  const char* t = "[Product]\nId=Pica\nName=Pica\nVersion=1.0\nMainExe=pica.exe\n[Files]\nFile=pica.exe\n[Types]\n"
                  "Group=Text files | .txt* .log* .ini* .nfo\nGroup=Web pages (now in your browser) | .htm .html .xml\n"
                  "Group=Source code | .c* .cpp* .h* .hpp .rc .asm* .py* .css .md* .mak* .diff\n"
                  "Group=Scripts and setup files | .bat .cmd .reg .inf .vbs .js | edit | note=Double-click still runs these\n";
  int ok = manifest_parse(t, (unsigned int)strlen(t), &m, MP_PACK, err, sizeof(err));
  check(ok, "pica types manifest");
  check(types_layout(&m, b), "pica layout fits");
  check(b[0].x == 7 && b[0].y == 30 && b[0].w == 166, "text box like Pica (7,30,166)");
  check(b[1].x == 178 && b[1].y == 30 && b[1].w == 132, "web box like Pica (178,30,132)");
  check(b[2].x == 7 && b[2].y == 59 && b[2].w == 303 && b[2].cols == 7, "source box like Pica (7,59,303)");
  check(b[3].x == 7 && b[3].y == 100 && b[3].top == 121, "script box like Pica (7,100), note row");
}

int main(int argc, char** argv)
{
  int i;
  test_wild();
  test_versions();
  test_layout();
  for (i = 1; i < argc; i++) test_file(argv[i]);
  printf("mantest: %d checks, %d failed (%d manifests)\n", g_checks, g_fail, argc - 1);
  return g_fail ? 1 : 0;
}
