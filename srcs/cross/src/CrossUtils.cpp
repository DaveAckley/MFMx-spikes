#include <stdarg.h>
#include "CrossUtils.h"
#include "Fail.h"
#include "XUtils.h"

namespace MFM {

  void memset_s(void* addr, u8 byte, u32 count) {
    memset(addr,byte,count); // don't have explicit_bzero in libs I'm using?
  }

  int strcmp_s(const char *s1, const char *s2) {
    MFM_API_ASSERT(s1 && s2,ILLEGAL_ARGUMENT);
    while (*s1 && *s2) {
      int d = *s1++ - *s2++;
      if (d) return d;
    }
    if (*s1) return 1;
    if (*s2) return -1;
    return 0;
  }

  //extern void XXX_DEBUG_FUNC(const char * file, u32 line) __attribute__ ((used)) ;

  static u8 * getu8fog(u32 p) {
    volatile union { u32 a; u8 * b; } c;
    c.a = p;
    return c.b;
  }
  static u32 * getu32fog(u32 p) {
    volatile union { u32 a; u32 * b; } c;
    c.a = p;
    return c.b;
  }

  const char * stripDirs(const char * path, u32 dircount) {
    const char * suf = path;
    while (*++suf != '\0') { } // get to end
    for (u32 i = 0u; i <= dircount; ++i) {  // keep only dircount dirs to save log space
      while (suf != path && *--suf != '/') { }
    }
    if (!*suf) suf = path;
    return suf;
  }

}
