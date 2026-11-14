#pragma once     /* -*- C++ -*- */

#include "itype.h"
#include "BlockCode.h"

namespace MFM {
  struct EndPointAddress {
    BlockCode mBlockCode;
    u8 mBlockCodeIndex;
  };
  
  struct EPBase {
    //    const char * getName() const ;
    EndPointAddress mSrcEPA; //< Local end
    EndPointAddress mDestEPA;   //< Remote end

    EndPointAddress getSrcEPA() const { return mSrcEPA; }
    BlockCode getSrcBlockCode() const { return getSrcEPA().mBlockCode; }
    u8 getSrcBlockCodeIndex() const { return getSrcEPA().mBlockCodeIndex; }

    EndPointAddress getDestEPA() const { return mDestEPA; }
    BlockCode getDestBlockCode() const { return getDestEPA().mBlockCode; }
    u8 getDestBlockCodeIndex() const { return getDestEPA().mBlockCodeIndex; }
  };
}
