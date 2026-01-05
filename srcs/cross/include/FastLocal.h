#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"
#include "Fail.h"
#include "U8C.h"

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
    /// DO NOT CHANGE THIS STRUCT WITHOUT CONSULTING _BUD.S ///
    u8 mHartNum, mInspirationOnHand;
    U8C mPos;              // local copy of HostBlock.mPos
    u32 mCreativityBuffer; // see FastT2.h
  };

  void sleepCycles(u32 cycles) ;

  extern FastAll fAll;
}
