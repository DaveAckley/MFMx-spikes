#pragma once        /* -*- C++ -*- */
#include "FastLocal.h"
#include "HostBlock.h"
#include "RandMT.h"
#include "Constants.h"
#include "ExtraConstants.h"

namespace MFM {

  extern u32 createByMail() ;

  extern u32 create(u32 max) /*__attribute__ ((optimize(3))) */;

  inline u32 between(u32 min, u32 max) {
    return create(max-min+1u)+min;
  }

  inline bool oddsOf(u32 thismany, u32 outofthismany) {
    return create(outofthismany)<thismany;
  }

  inline bool oneIn(u32 thismany) { return oddsOf(1u, thismany); }
  
#define COUNT_LEADING_ZEROS(ofnum) __builtin_clz(ofnum)

  inline u32 createBits(u8 bitsNeeded) {
    if (__builtin_expect(fAll.mInspirationOnHand < bitsNeeded,0)) {
      fAll.mCreativityBuffer = createByMail();
      fAll.mInspirationOnHand = 32u;
    }
    u32 creation = fAll.mCreativityBuffer & ((1u<<bitsNeeded)-1u);
    fAll.mCreativityBuffer >>= bitsNeeded;
    fAll.mInspirationOnHand -= bitsNeeded;
    return creation;
  }

  extern void preloadT2Mailbox() /*__attribute__ ((optimize(3))) */;
  extern int hartMainT2(HostBlock & hb) ;
}
