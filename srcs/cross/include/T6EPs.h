#pragma once   /* -*- C++ -*- */
#include "TCCommon.h" // for TCOpsData
#include "EP.h"
#include "BlockCode.h"
#include "RingBuffer.h"

namespace MFM {

  template<class SUBEP, class SUBTCBLOCK>
  struct T6ToT6EP : public EP<SUBEP,typename SUBTCBLOCK::CAR_TYPE> {
    using Super = EP<SUBEP,typename SUBTCBLOCK::CAR_TYPE>;
    using SUBTC = typename SUBTCBLOCK::CAR_TYPE;
    static constexpr u32 TC_BLOCK_SIZE = sizeof(SUBTCBLOCK);
    static constexpr u32 CAR_COUNT = SUBTCBLOCK::CAR_COUNT;
    static constexpr u32 CAR_SIZE = sizeof(SUBTC);

    static u8 constexpr MASK_BITS = _getLogBase2(CAR_COUNT+1u); // +1 chicken
    typedef RingBuffer<u8,MASK_BITS> CarIdxRB;

    static u8 constexpr TC2EP = 0u;
    static u8 constexpr EP2TC = 1u;
    struct CarIdxs { CarIdxRB mIdxs[2]; };
    
    bool shipTC(SUBTC & car, u8 carindex) ;
    void init(BlockCode bc, u8 blkIdx, bool isin, SUBTCBLOCK & stgblk, AtomicLock & al, CarIdxs & caridxs) ;

    SUBTCBLOCK * mCarStgPtr;
    CarIdxs * mCarIdxsPtr;
    TCCommon::TCOpsData mOpsDataStg[CAR_COUNT];

  };

}

#include "T6EPs.tcc"
