// port.h - portability helpers for the code shared by VC6 and clang (pack98, tests, manifest).
#ifndef S98_PORT_H
#define S98_PORT_H
#include <stdio.h>
#include <stdarg.h>

// bounded, always terminated sprintf (VC6 has only _vsnprintf, which doesn't terminate on overflow)
inline int s98_fmt(char* buf, int cap, const char* fmt, ...)
{
  va_list ap;
  int n;
  if (cap <= 0) return 0;
  va_start(ap, fmt);
#ifdef _MSC_VER
  n = _vsnprintf(buf, (size_t)cap, fmt, ap);
#else
  n = vsnprintf(buf, (size_t)cap, fmt, ap);
#endif
  va_end(ap);
  buf[cap - 1] = 0;
  return n;
}

#endif
