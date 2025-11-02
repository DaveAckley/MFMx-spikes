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
      return memcmp((const void*) this, (const void*) &other, sizeof(P4Atom));
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

    u16 mParityAndType; // (3+3+1) bit 2D even parity + 9 bits of type
    u16 mData0;         // First 16 bits of user state
    u32 mStg[2];        // Remaining 64 bits of user state
  };
}


