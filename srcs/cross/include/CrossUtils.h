#pragma once /* -*- C++ -*- */

#include "itype.h"

/// SEE NOTE BELOW ABOUT XXX_MAYBE_DIR_FUNC
#define DIEWAY() /*DIEWAYDOIT()*/
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

#define XXX_DEBUG_FUNC(...) /* XXX_DEBUG_FUNC_DOIT(__FILE__,__LINE__) */

namespace MFM {
  void memset_s(void* addr, u8 byte, u32 count) ;

  int strcmp_s(const char *s1, const char *s2) ;

  void XXX_DEBUG_FUNC_DOIT(const char * file, u32 line) ;

  /*** NOTE: XXX_MAYBE_DIE_FUNC IS HARDCODED TO ONE IMAGIC CONSTANT
       IT IS NOT NOT NOT NOT FOR GENERAL USE
   ***/
  void XXX_MAYBE_DIE_FUNC(const char * file, u32 line, u32& count);

  const char * stripDirs(const char * path, u32 dircount = 2u);
}
