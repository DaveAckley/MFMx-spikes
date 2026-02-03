#pragma once /* -*- C++ -*- */

#include "itype.h"
#include <string.h>

#define DIEWAY() DIEWAYDOIT()
#define DIEWAYDOIT() do {                       \
  static u32 count;                             \
  XXX_MAYBE_DIE_FUNC(__FILE__,__LINE__,count);  \
  } while (0)

#define SHOWADDR(var) /*SHOWADDRDOIT(var)*/
#define SHOWADDRDOIT(var)do {                           \
  _Pragma("GCC diagnostic push")                        \
  _Pragma("GCC diagnostic ignored \"-Wstrict-aliasing\"")     \
  DP.printf(                                    \
  "%s:%d:(%d,%d,%s)SHD:%s@0x%x=0x%x/%d\n",      \
    stripDirs(__FILE__),__LINE__,               \
    fAll.mPos.x, fAll.mPos.y,                   \
    hartName(fAll.mHartNum),                    \
    #var, (u32) &(var),                         \
    *(u32*)&(var), *(u32*)&(var));              \
  _Pragma("GCC diagnostic pop")                 \
  } while (0)                                 

namespace MFM {
  void memset_s(void* addr, u8 byte, u32 count) ;

  void XXX_DEBUG_FUNC(const char * file, u32 line) ;

  void XXX_MAYBE_DIE_FUNC(const char * file, u32 line, u32& count);

  const char * stripDirs(const char * path, u32 dircount = 2u);
}
