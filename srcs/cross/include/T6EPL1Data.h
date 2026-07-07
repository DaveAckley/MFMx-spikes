#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "utils.h"
#include "RingBuffer.h"
#include "AtomicLock.h"
#include "EPState.h"
#include "Debug.h"

namespace MFM {
  template<class SUBTCBLOCKSTG, u8 BLOCKCOUNT>
  struct T6EPL1Data {
    static constexpr u32 CAR_COUNT = SUBTCBLOCKSTG::CAR_COUNT;
    static u8 constexpr BLOCK_COUNT = BLOCKCOUNT;
    static u8 constexpr MASK_BITS = _getLogBase2(2u*CAR_COUNT+1u); // +1 chicken 2* superchicken
    typedef RingBuffer<u8,MASK_BITS> CarIdxRB;
    struct CarIdxs {
      static u8 constexpr COMM2COMP = 0u;
      static u8 constexpr COMP2COMM = 1u;
      CarIdxRB mTheIdxs[2];
    };

    using SUBTCBLOCK = typename SUBTCBLOCKSTG::CAR_TYPE;

    bool isUninitted(u8 blockindex) {
      return getPublicEPState(blockindex) == EPState::UNINITTED;
    }

    bool isActive(u8 blockindex) {
      return getPublicEPState(blockindex) == EPState::ACTIVE;
    }

    void reset() {
      memset_s(this,'\0',sizeof(*this));
    }

    SUBTCBLOCKSTG & getCarStg(u8 blockindex) {
      HBASSERT_LS(blockindex,BLOCK_COUNT);
      return mTheTCStorages[blockindex];
    }

    AtomicLock & getAtomicLock(u8 blockindex) {
      HBASSERT_LS(blockindex,BLOCK_COUNT);
      return mTheLocks[blockindex];
    }

    CarIdxs & getCarIdxs(u8 blockindex) {
      HBASSERT_LS(blockindex,BLOCK_COUNT);
      return mTheCarIdxs[blockindex];
    }

    EPState & getPublicEPState(u8 blockindex) {
      HBASSERT_LS(blockindex,BLOCK_COUNT);
      return mThePublicEPState[blockindex];
    }

    SUBTCBLOCKSTG mTheTCStorages[BLOCK_COUNT];
    AtomicLock mTheLocks[BLOCK_COUNT];
    CarIdxs mTheCarIdxs[BLOCK_COUNT];
    EPState mThePublicEPState[BLOCK_COUNT];
  };

}
