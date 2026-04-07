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

#define FAST_LOCAL_ARRAY(typedecl,count,name,hart)             \
  typedecl                                                     \
  name[count]                                                  \
  __attribute__ ((section(XSTR_MACRO(CONC(.fastram_,hart)))))  \

// Extra assertions if we know about fALL
#define MFM_API_ASSERT_ON_HART(expr) MFM_API_ASSERT(fAll.mHartNum==(expr),WRONG_HART)
#define MFM_API_ASSERT_NOT_ON_HART(expr) MFM_API_ASSERT(fAll.mHartNum!=(expr),WRONG_HART)
#define HBASSERT_ON_HART(expr) HBASSERT_EQ(fAll.mHartNum,expr)
#define HBASSERT_NOT_ON_HART(expr) HBASSERT_NE(fAll.mHartNum,expr)

namespace MFM {
  struct FastAll { 
    /// DO NOT CHANGE THIS STRUCT WITHOUT CONSULTING _BUD.S ///
    u8 mHartNum, mInspirationOnHand;
    U8C mNoC0;             // local copy of HostBlock.mNoC0
    u32 mCreativityBuffer; // see FastT2.h
  };

  void sleepCycles(u32 cycles) ;
  static constexpr u32 BREATH_DURATION = 8'000u; //< in fake 'cycles' which are some k/AIFreq with k > 1
  inline void breathe() { sleepCycles(BREATH_DURATION); } //< about 1ms?

  extern FastAll fAll;
}
