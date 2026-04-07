#pragma once   /* -*- C++ -*- */
#include "TCCommon.h" // for TCOpsData
#include "EP.h"
#include "EPState.h"
#include "BlockCode.h"
#include "RingBuffer.h"
#include "FastLocal.h" // for MFM_API_ASSERT_ON_HART
#include "T6CellO.h"
#include "T6EPL1Data.h"

namespace MFM {

  template<class SUBEP, class SUBTCBLOCKSTG, u8 BLOCK_COUNT, u8 FORHART>
  struct T6EP : public EP<SUBEP,typename SUBTCBLOCKSTG::CAR_TYPE> {
    static void ASSERT_RIGHT_HART() { MFM_API_ASSERT_ON_HART(FORHART); }

    using L1Data = T6EPL1Data<SUBTCBLOCKSTG,BLOCK_COUNT>;
    using SUBTC = typename SUBTCBLOCKSTG::CAR_TYPE;
    using Super = EP<SUBEP,SUBTC>;
    static constexpr u32 TC_BLOCK_SIZE = sizeof(SUBTCBLOCKSTG);
    static constexpr u32 CAR_COUNT = SUBTCBLOCKSTG::CAR_COUNT;
    static constexpr u32 CAR_SIZE = sizeof(SUBTC);

    void setPublicEPState(EPState newstate) ;
    SUBTC * getClosedTCPtrIfAny() const ;

    bool shipTC(SUBTC & car, u8 carindex) ;
    void initT6EP(BlockCode bc, bool isin, L1Data &l1Data) ;
    void configureDest(U8C ournoc0, U8C destnoc0, u8 blockindex) ;

    TCOpsData mOpsDataStg[CAR_COUNT];
    L1Data * mL1Data;

    L1Data & getL1Data() {
      MFM_API_ASSERT_NONNULL(mL1Data);
      u8 idx = this->getDestBlockCodeIndex();
      return *mL1Data;
    }

    L1Data & getL1Data() const {
      MFM_API_ASSERT_NONNULL(mL1Data);
      u8 idx = this->getDestBlockCodeIndex();
      return *mL1Data;
    }

    SUBTCBLOCKSTG & getCarStg() const {
      return getL1Data().getCarStg(this->getDestBlockCodeIndex());
    }

    AtomicLock & getAtomicLock() const {
      return getL1Data().getAtomicLock(this->getDestBlockCodeIndex());
    }

    typename L1Data::CarIdxs & getCarIdxs() const {
      return getL1Data().getCarIdxs(this->getDestBlockCodeIndex());
    }

    EPState & getPublicEPState() const {
      return getL1Data().getPublicEPState(this->getDestBlockCodeIndex());
    }

    U8C mDestNoC0;
    u32 mDestBlockAddr;         // blockindex already applied; needs only carindex

  };


}

#include "T6EPs.tcc"
