#pragma once   /* -*- C++ -*- */
#include "TCCommon.h"

namespace MFM {
  template <class CART,u32 CARS>
  struct alignas(16) TCStorage {
    using CAR_TYPE = CART;
    static constexpr u32 CAR_COUNT = CARS;
    static bool validIndex(u32 idx) { return idx < CAR_COUNT; }

    u32 getCarSize() const { return sizeof(CART); }
    u32 getCarCount() const { return CAR_COUNT; }

    CAR_TYPE& getTC(u32 idx) {
      HBASSERT_LT(idx,CAR_COUNT);
      return mTCs[idx];
    }
    CAR_TYPE const & getTC(u32 idx) const {
      HBASSERT_LT(idx,CAR_COUNT);
      return mTCs[idx];
    }

  private:
    CAR_TYPE mTCs[CAR_COUNT];
  };
}
