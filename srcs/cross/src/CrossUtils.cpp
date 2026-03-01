#include <stdarg.h>
#include "CrossUtils.h"
#include "Printf.h"
#include "FastLocal.h" // for fAll

namespace MFM {

  void CUprintf(u32 count, const char * file, u32 line, const char * fmt, ...) {
    file = stripDirs(file);
    DP.printf("%s:%u:[%u] ",file,line,count);
    va_list ap;
    va_start(ap,fmt);
    DP.vprintf(fmt,ap);
    va_end(ap);
  }

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
    for (u32 i = 0u; i <= dircount; ++i) {  // keep only dircount dirs to save space
      while (suf != path && *--suf != '/') { }
    }
    if (!*suf) suf = path;
    return suf;
  }

  void XXX_MAYBE_DIE_FUNC(const char * file, u32 line, u32 & count) {
    constexpr bool PRINT_FIRST = false;
    ++count;
    u32 val = *getu32fog(0x14+4);
    bool good = (val == 0x000101ba);
                             
    if ((PRINT_FIRST && count == 1) || !good) {
    const char * suf = stripDirs(file);
    DP.printf("%s:%d:(%d,%d,%s)XG%s#%d %x\n",
              suf, line,
              fAll.mPos.x, fAll.mPos.y,
              hartName(fAll.mHartNum),
              good?"OK":"DIE",
              count,
              val);
    }
    if (!good)
      t6hang(DIE_WAY);
  }

  void XXX_DEBUG_FUNC_DOIT(const char * file, u32 line) {
    if (fAll.mPos.x == 2u && fAll.mPos.y == 3u) {
      //#pragma GCC diagnostic push 
      //#pragma GCC diagnostic ignored "-Warray-bounds"  // Aarrgh
    const char * suf = stripDirs(file);
    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'
    const u8 ibux17 = *getu8fog(0x14+3u);
    const u32 ibux18 = *getu32fog(0x14+4u);
    const u32 ibux1c = *getu32fog(0x14+8u);
    const u32 ibux20 = *getu32fog(0x14+12u);
    DP.printf("%s:%d:XG[%s,%d]%x.%x.%x.%x\n",
              suf, line,
              hartName(fAll.mHartNum),fAll.mHartNum,
              ibux17, ibux18, ibux1c, ibux20);
    //#pragma GCC diagnostic pop
    }
  }
}
