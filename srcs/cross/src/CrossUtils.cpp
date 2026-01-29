#include "CrossUtils.h"
#include "FastLocal.h"
#include "Printf.h"

namespace MFM {
  void memset_s(void* addr, u8 byte, u32 count) {
    memset(addr,byte,count); // don't have explicit_bzero in libs I'm using?
  }

  void XXX_DEBUG_FUNC(const char * file, u32 line, const char * msg) {
    if (fAll.mPos.x == 2u && fAll.mPos.y == 3u) {
#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Warray-bounds"  // Aarrgh
    const char * suf = file;
    for (u32 i = 0u; i < 3u; ++i) {  // skip some dirs to save space
      while (*suf != 0 && *suf++ != '/') { }
    }
    if (!*suf) suf = file;
    const volatile u32 *ibu = (u32*) 0x14;  // '= &theImageBlock;'
    DP.printf("%s:%d:XDB[%s,%d]%x.%08x.%08x.%08x%s\n",
              suf, line,
              hartName(fAll.mHartNum),fAll.mHartNum,
              ((char*)&ibu[0])[1],
              ibu[1],ibu[2],ibu[3],
              msg?msg:"");
#pragma GCC diagnostic pop
    }
  }
}
