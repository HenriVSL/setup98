// wild.cpp - path patterns (see wild.h).
#include "wild.h"

static char fold(char c)
{
  return (c >= 'a' && c <= 'z') ? (char)(c - 32) : (c == '/' ? '\\' : c);
}

static int match_at(const char* p, const char* s, int depth)
{
  if (depth > 64) return 0;
  while (*p) {
    if (p[0] == '*' && p[1] == '*') {
      const char* t;
      p += 2;
      while (*p == '*') p++;
      if (*p == '\\' || *p == '/') {         // "**\": zero or more whole folders
        p++;
        t = s;
        for (;;) {
          if (match_at(p, t, depth + 1)) return 1;
          while (*t && *t != '\\' && *t != '/') t++;
          if (!*t) return 0;
          t++;
        }
      }
      if (!*p) return 1;
      for (t = s;; t++) {
        if (match_at(p, t, depth + 1)) return 1;
        if (!*t) return 0;
      }
    }
    if (*p == '*') {
      const char* t;
      p++;
      for (t = s;; t++) {
        if (match_at(p, t, depth + 1)) return 1;
        if (!*t || *t == '\\' || *t == '/') return 0;
      }
    }
    if (*p == '?') {
      if (!*s || *s == '\\' || *s == '/') return 0;
    } else if (fold(*p) != fold(*s)) {
      return 0;
    }
    p++;
    s++;
  }
  return *s == 0;
}

int wild_match(const char* pat, const char* path)
{
  return match_at(pat, path, 0);
}

int wild_has(const char* pat)
{
  for (; *pat; pat++) if (*pat == '*' || *pat == '?') return 1;
  return 0;
}

int path_cmp_i(const char* a, const char* b)
{
  for (;; a++, b++) {
    char x = fold(*a), y = fold(*b);
    if (x != y) return (unsigned char)x < (unsigned char)y ? -1 : 1;
    if (!x) return 0;
  }
}
