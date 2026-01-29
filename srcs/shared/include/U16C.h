#ifndef U16C_H
#define U16C_H

#include "itype.h"

namespace MFM {
  struct U16C {
    u16 x, y;

    bool operator==(const U16C other) const {
      return x == other.x && y == other.y;
    }

    static U16C makeCT6CoordFromTLBI(uint32_t tlbidx) {
      U16C ret;
      ret.x = tlbidx%14u;
      ret.y = tlbidx/14u;
      return ret;
    }

    static U16C makeNocCoordFromTLBI(uint32_t tlbidx) {
      U16C ret = makeCT6CoordFromTLBI(tlbidx);
      ret.x++;
      if (ret.x > 7u) ret.x += 2u;
      ret.y += 2u;
      return ret;
    }

    static u32 makeTLBIFromCT6Coord(U16C c) {
      u32 ret = c.y*14u + c.x;
      return ret;
    }

    static u32 makeTLBIFromNocCoord(U16C c) {
      c.y -= 2u;
      if (c.x > 9u) c.x -= 2u;
      c.x--;
      return makeTLBIFromCT6Coord(c);
    }
  };

  struct U16CRange {
    U16C start, end;

    u32 area() const {
      return
        (end.x - start.x +1) *
        (end.y - start.y +1);
    }
  };
}

#endif /* U16C_H */
