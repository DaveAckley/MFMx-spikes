#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "MDist.h"

namespace MFM {
  struct Wrap8 {
    static constexpr u8 MIDPOINT = U8_MAX/2u;
    u8 mData;

    bool isLess(const Wrap8 & other) const {
      if (mData < other.mData) return (other.mData - mData) < MIDPOINT;
      return (mData - other.mData) >= MIDPOINT;
    }
    bool isComparable(const Wrap8 & other) const {
      return *this <= other || *this >= other;
    }
    bool operator==(const Wrap8 & other) const { return mData == other.mData; }
    bool operator!=(const Wrap8 & other) const { return !(*this == other); }
    bool operator<(const Wrap8 & other) const { return isLess(other); }
    bool operator<=(const Wrap8 & other) const { return *this == other || *this < other; }
    bool operator>(const Wrap8 & other) const { return other.isLess(*this); }
    bool operator>=(const Wrap8 & other) const { return *this == other || *this > other; }

    void increment() { increment(1); }
    void increment(u8 amt) { mData += amt; /* wrapping on overflow */ }
    Wrap8 & operator+=(u8 amt) { this->increment(amt); return *this; }

    void decrement() { decrement(1); }
    void decrement(u8 amt) { mData += U8_MAX - amt; }
    Wrap8 & operator-=(u8 amt) { this->decrement(amt); return *this; }
  };

  union CornerState {
    u32 mWord;
    Wrap8 mState[4]; // [Corner4]

    void init() { mWord = 0u; }

    bool isValid(Corner4 & minCorner) const {
      Corner4 minc = C4_MIN;
      Wrap8 min = mState[(u8)minc];
      for (u32 i = C4_MIN; i <= C4_MAX; ++i)
        for (u32 j = C4_MIN; j <= C4_MAX; ++j)
          if (i == j) continue;
          else if (mState[i] == mState[j] ||
                   !mState[i].isComparable(mState[j]))
            return false;
          else if (mState[i].isLess(min)) {
            minc = (Corner4) i;
            min = mState[i];
          }
      minCorner = minc;
      return true;
    }
  };
}
