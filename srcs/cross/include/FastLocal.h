#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"
#include "Fail.h"
#include "UxC.h" // for U8C
#include "ImageBlock.h"

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
    u8 mHartNum;           // [0]
    u8 mInspirationOnHand; // [1]
    U8C mNoC0;             // [2..3] local copy of HostBlock.mNoC0
    u32 mCreativityBuffer; // [4..7] see FastT2.h
    u8 mImageCode;         // [8] copy of T6_IMAGE_BLOCK_ADDR ImageBlockHeader -
    u8 mEntries;           // [9] - fields of the same names
    u16 mHeartSpin;        // [10..11] see StandardLife.cpp
  };

  void sleepCycles(u32 cycles) ;
  static constexpr u32 BREATH_DURATION = 3'000u; //< in fake 'cycles' which are some k/AIFreq with k > 1
  inline void breathe() { sleepCycles(BREATH_DURATION); } //< ~<1ms?

  extern FastAll fAll;
}
