#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "MDist.h"

namespace MFM {
  struct Wrap8 {
    static constexpr u8 MIDPOINT = U8_MAX/2u;
    u8 mData;

    s8 subtract(const Wrap8 & other) const {
      if (isLess(other)) return -(other.mData - mData);
      if (other.isLess(*this)) return -(mData - other.mData);
      return 0;
    }
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

    Wrap8 & increment() { return increment(1); }
    Wrap8 & increment(u8 amt) { mData += amt; return *this; } /* just wrap on overflow */ 
    Wrap8 & operator+=(u8 amt) { this->increment(amt); return *this; }

    Wrap8 & decrement() { return decrement(1); }
    Wrap8 & decrement(u8 amt) { mData += U8_MAX - amt; return *this; }
    Wrap8 & operator-=(u8 amt) { this->decrement(amt); return *this; }
  };
}
