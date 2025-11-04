#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"
#include "Fail.h"

// See cross/src/_BUD.ld.in for section defs and such

#define FAST_LOCAL(typedecl,name,hart)                         \
  typedecl                                                     \
  name                                                         \
  __attribute__ ((section(XSTR_MACRO(CONC(.fastram_,hart)))))  \

// Extra assertions if we know about fALL
#define MFM_API_ASSERT_ON_HART(expr) MFM_API_ASSERT(fAll.mHartNum==(expr),WRONG_HART)
#define MFM_API_ASSERT_NOT_ON_HART(expr) MFM_API_ASSERT(fAll.mHartNum!=(expr),WRONG_HART)

namespace MFM {
  struct FastAll {
    u8 mHartNum, mXPos, mYPos, mInspirationOnHand;
    u32 mCreativityBuffer; // see FastT2.h
  };

  void sleepCycles(u32 cycles) ;

  extern FastAll fAll;
}
