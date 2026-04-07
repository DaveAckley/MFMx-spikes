#pragma once   /* -*- C++ -*- */
#include "TCCommon.h"

namespace MFM {
  template <class CART,u32 CARS>
  struct alignas(16) TCBlock {
    using CAR_TYPE = CART;
    static constexpr u32 CAR_COUNT = CARS;
    static bool validIndex(u32 idx) { return idx < CAR_COUNT; }

    u32 getCarSize() const { return sizeof(CART); }
    u32 getCarCount() const { return CAR_COUNT; }

    CAR_TYPE& getTC(u32 idx) {
      MFM_API_ASSERT(validIndex(idx),ILLEGAL_ARGUMENT);
      return mTCs[idx];
    }
    CAR_TYPE const & getTC(u32 idx) const {
      MFM_API_ASSERT(validIndex(idx),ILLEGAL_ARGUMENT);
      return mTCs[idx];
    }

  private:
    CAR_TYPE mTCs[CAR_COUNT];
  };
}
