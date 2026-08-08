#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

namespace MFM {

  static constexpr u32 ACB1_BLOCK_COUNT = 1u;
  struct EP_ACBL1 : public T6EP<EP_ACBL1,ACBlockStg,ACB1_BLOCK_COUNT,HARTNUM_NC> {
    using Super = T6EP<EP_ACBL1,ACBlockStg,ACB1_BLOCK_COUNT,HARTNUM_NC>;

    static constexpr u32 CAR_COUNT = Super::SUBTCBLOCKSTG::CAR_COUNT;
    static u8 constexpr BLOCK_COUNT = BLOCK_COUNT;
    static u8 constexpr MASK_BITS = _getLogBase2(2u*CAR_COUNT+1u); // +1 chicken 2* superchicken
    typedef RingBuffer<u8,MASK_BITS> CarIdxRB;
    struct CarIdxs {
      static u8 constexpr COMM2COMP = 0u;
      static u8 constexpr COMP2COMM = 1u;
      CarIdxRB mTheIdxs[2];
    };

    using SUBTCBLOCK = typename SUBTCBLOCKSTG::CAR_TYPE;
    
    using ACBL1Data = T6EPL1Data<ACBlockStg,ACB_BLOCK_COUNT>;

    ACBL1Data mPublicData;

  };
}

