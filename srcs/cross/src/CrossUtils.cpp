#include "CrossUtils.h"
#include "Printf.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  void memset_s(void* addr, u8 byte, u32 count) {
    memset(addr,byte,count); // don't have explicit_bzero in libs I'm using?
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
    for (u32 i = 0u; i < dircount; ++i) {  // skip some dirs to save space
      while (*suf != 0 && *suf++ != '/') { }
    }
    if (!*suf) suf = path;
    return suf;
  }

  void XXX_MAYBE_DIE_FUNC(const char * file, u32 line, u32 & count) {
    ++count;
    u32 val = *getu32fog(0x14+4);
    if (val == 0x000101ba) return;
                             
    const char * suf = stripDirs(file);
    DP.printf("%s:%d:(%d,%d,%s)XGDIE#%d\n",
              suf, line,
              fAll.mPos.x, fAll.mPos.y,
              hartName(fAll.mHartNum),
              count);
    t6hang(DIE_WAY);
  }

  void XXX_DEBUG_FUNC(const char * file, u32 line) {
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
