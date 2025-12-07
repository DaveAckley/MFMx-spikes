#include "P4Atom.h"

namespace MFM {
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

  u16 P4Atom::addParity(u16 d) {
    for (u32 i = 0u; i < sizeof(MASKS2D3X3)/sizeof(MASKS2D3X3[0]); ++i) 
      if (__builtin_popcount(d & MASKS2D3X3[i])&1)
        d ^= PARITYBITS[i];     // Make even parity everywhere
    return d;
  }

  bool P4Atom::isValidParity(u16 d) {
    if ((d&0x1ff) == INACCESSIBLE_TYPE)
      return false;
    for (u32 i = 0u; i < sizeof(MASKS2D3X3)/sizeof(MASKS2D3X3[0]); ++i) 
      if (__builtin_popcount(d & MASKS2D3X3[i])&1) return false;
    return true;
  }

  P4Atom P4Atom::makeAtom(u16 type) {
    P4Atom a;
    if (type > MAX_TYPE) type = INACCESSIBLE_TYPE; // aka MAX_TYPE..
    a.mParityAndType = P4Atom::addParity(type);
    return a;
  }
}
