#pragma once        /* -*- C++ -*- */
#include "FastLocal.h"
#include "HostBlock.h"
#include "RandMT.h"
#include "Constants.h"
#include "ExtraConstants.h"

namespace MFM {

  extern u32 createByMail() ;

  inline u32 createBits(u8 bitsNeeded) {
    if (fAll.mInspirationOnHand < bitsNeeded) {
      fAll.mCreativityBuffer = createByMail();
      fAll.mInspirationOnHand = 32u;
    }
    u32 creation = fAll.mCreativityBuffer & ((1u<<bitsNeeded)-1u);
    fAll.mCreativityBuffer >>= bitsNeeded;
    fAll.mInspirationOnHand -= bitsNeeded;
    return creation;
  }

  extern void preloadT2Mailbox() __attribute__ ((optimize(3))) ;
  extern int hartMainT2(HostBlock & hb) ;
}
