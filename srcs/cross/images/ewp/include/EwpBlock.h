#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCBlock.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "SharedTCs.h" // for EwpPayload and EwpBlock

namespace MFM {

#if 0
  struct EwpBlockStg : TCBlock<EwpBlock,2> { // umm this struct could have been a typedef
    typedef TCBlock<EwpBlock,2> Super;
  };
#else
  typedef TCBlock<EwpBlock,2> EwpBlockStg;
#endif

  struct EwpEP : public T6EP<EwpEP,EwpBlockStg> {
    using Super = T6EP<EwpEP,EwpBlockStg>;

    //// EP API
    const char * getName() const { return isIn() ? "EwpEP-i" : "EwpEP-o"; }
    u32 getCarSize() const { return sizeof(EwpBlock); }
    EwpBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      MFM_API_ASSERT(carindex < CAR_COUNT,ILLEGAL_ARGUMENT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(EwpBlock & car, u8 carindex) ;
    //    bool shipTC(EwpBlock & car, u8 carindex) ; ..handled by T6EP

    void init(BlockCode destbc, u32 destidx, bool isin, EwpBlockStg & cars, AtomicLock & al, CarIdxs & caridxs) ;
  };

  extern EwpBlockStg theEwpBlockCars[1];

}

