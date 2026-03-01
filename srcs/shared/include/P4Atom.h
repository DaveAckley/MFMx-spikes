#pragma once /* -*- C++ -*- */

#include "itype.h"
#include <string.h> // for memcmp

namespace MFM {
  extern void memset_s(void*, u8, u32) ;

  struct P4Atom {
    static constexpr u16 MAX_TYPE = (1u<<9)-1u;
    static constexpr u16 EMPTY_TYPE = 0u;
    static constexpr u16 START_TYPE = 1u;
    static constexpr u16 INACCESSIBLE_TYPE = MAX_TYPE;

    P4Atom() { memset_s(this, 0u, sizeof(P4Atom)); }
    s32 compareTo(const P4Atom other) const {
      const u32 * us = (u32*) this;
      const u32 * ot = (u32*) &other;
      if (us[0] < ot[0]) return -1;
      if (us[0] > ot[0]) return  1;
      if (us[1] < ot[1]) return -1;
      if (us[1] > ot[1]) return  1;
      if (us[2] < ot[2]) return -1;
      if (us[2] > ot[2]) return  1;
      return 0;
      /*
      if (mParityAndType < other.mParityAndType) return -1;
      if (mParityAndType > other.mParityAndType) return 1;
      if (mData0 < other.mData0) return -1;
      if (mData0 > other.mData0) return 1;
      if (mStg[0] < other.mStg[0]) return -1;
      if (mStg[0] > other.mStg[0]) return 1;
      if (mStg[1] < other.mStg[1]) return -1;
      if (mStg[1] > other.mStg[1]) return 1;
      return 0;
      */
    }
    bool operator==(const P4Atom other) const { return compareTo(other) == 0; }
    bool operator!=(const P4Atom other) const { return compareTo(other) != 0; }
    bool operator<(const P4Atom other) const { return compareTo(other) < 0; }
    bool operator<=(const P4Atom other) const { return compareTo(other) <= 0; }

    static u16 addParity(u16 ninebitsdata) ;
    static P4Atom makeAtom(u16 type) ;
    inline static P4Atom makeEmptyAtom() { return makeAtom(EMPTY_TYPE); }
    inline static P4Atom makeStartAtom() { return makeAtom(START_TYPE); }
    inline static P4Atom makeInaccessibleAtom() { return makeAtom(INACCESSIBLE_TYPE); }

    static bool isValidParity(u16 dataplusparity) ;
    inline bool isValid() const { return isValidParity(mParityAndType); }
    inline u32 getType() const { return mParityAndType & 0x1ff; }

    u16 mParityAndType; // 16 bits: (3+3+1) bit 2D even parity + 9 bits of type
    u16 mData0;         // Then first 16 bits of user state
    u32 mStg[2];        // Then remaining 64 bits of user state
  };
}


