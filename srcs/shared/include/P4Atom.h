#pragma once /* -*- C++ -*- */

#include "itype.h"
#include <string.h> // for memcmp

namespace MFM {
  extern void memset_s(void*, u8, u32) ;

  /* LAYOUT
     P6 D8 D7 D6
     P5 D5 D4 D3
     P4 D2 D1 D0
     P3 P2 P1 P0

     15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
     P6 P5 P4 P3 P2 P1 P0 D8 D7 D6 D5 D4 D3 D2 D1 D0
     +-----------+-----------+-----------+----------+
     |1  0  0  0 |0  0  0  1 |1  1  0  0 |0  0  0  0| Row 1 0x81c0
     |0  1  0  0 |0  0  0  0 |0  0  1  1 |1  0  0  0| Row 2 0x4038
     |0  0  1  0 |0  0  0  0 |0  0  0  0 |0  1  1  1| Row 3 0x2007
     |0  0  0  1 |1  1  1  0 |0  0  0  0 |0  0  0  0| Row 4 0x1e00
     |1  1  1  1 |0  0  0  0 |0  0  0  0 |0  0  0  0| Col 1 0xf000
     |0  0  0  0 |1  0  0  1 |0  0  1  0 |0  1  0  0| Col 2 0x0924
     |0  0  0  0 |0  1  0  0 |1  0  0  1 |0  0  1  0| Col 3 0x0492
     |0  0  0  0 |0  0  1  0 |0  1  0  0 |1  0  0  1| Col 4 0x0249
     +-----------+-----------+-----------+----------+
  */
  struct P4Atom {
    static constexpr u16 MAX_TYPE = (1u<<9)-1u;
    static constexpr u16 EMPTY_TYPE = 0u;
    static constexpr u16 START_TYPE = 1u;
    static constexpr u16 INACCESSIBLE_TYPE = MAX_TYPE;

    P4Atom() = default;
    void init() { memset_s(this, 0u, sizeof(P4Atom)); }
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
    }
    bool operator==(const P4Atom other) const { return compareTo(other) == 0; }
    bool operator!=(const P4Atom other) const { return compareTo(other) != 0; }
    bool operator<(const P4Atom other) const { return compareTo(other) < 0; }
    bool operator<=(const P4Atom other) const { return compareTo(other) <= 0; }

    static constexpr u16 MASKS2D3X3[] = {
      /* In a suitable order to compute..
      row1    row2    row3    row4    col4    col3    col2    col1 */
      0x81c0, 0x4038, 0x2007, 0x1e00, 0x0249, 0x0492, 0x0924, 0xf000
    };
    static constexpr u16 PARITYBITS[] = {
      /* Corresponding to above masks
      row1    row2    row3    row4    col4    col3    col2    col1 */
      0x8000, 0x4000, 0x2000, 0x1000, 0x0200, 0x0400, 0x0800, 0x1000
    };

    static constexpr u16 addParity(u16 d) {
      for (u32 i = 0u; i < sizeof(MASKS2D3X3)/sizeof(MASKS2D3X3[0]); ++i) 
        if (__builtin_popcount(d & MASKS2D3X3[i])&1)
          d ^= PARITYBITS[i];     // Make even parity everywhere
      return d;
    }

    static constexpr bool isValidParity(u16 d) {
      if ((d&0x1ff) == INACCESSIBLE_TYPE)
        return false;
      for (u32 i = 0u; i < sizeof(MASKS2D3X3)/sizeof(MASKS2D3X3[0]); ++i) 
        if (__builtin_popcount(d & MASKS2D3X3[i])&1) return false;
      return true;
    }

    static constexpr P4Atom makeAtom(u16 type) {
      P4Atom a{};
      if (type > MAX_TYPE) type = INACCESSIBLE_TYPE; // aka MAX_TYPE..
      a.mParityAndType = P4Atom::addParity(type);
      return a;
    }

    static constexpr P4Atom makeEmptyAtom() { return makeAtom(EMPTY_TYPE); }
    static constexpr P4Atom makeStartAtom() { return makeAtom(START_TYPE); }
    static constexpr P4Atom makeInaccessibleAtom() { return makeAtom(INACCESSIBLE_TYPE); }

    inline bool isValid() const { return isValidParity(mParityAndType); }
    inline u32 getType() const { return mParityAndType & 0x1ff; }
    inline bool isEmpty() const { return getType() == EMPTY_TYPE; }

    u16 mParityAndType; // 16 bits: (3+3+1) bit 2D even parity + 9 bits of type
    u16 mData0;         // Then first 16 bits of user state
    u32 mStg[2];        // Then remaining 64 bits of user state
  };
}


