#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include <string>
#include "MDist.h"

namespace MFM {
  struct S8C {
    S8C() : x(0) , y(0) { }
    S8C(s32 sx, s32 sy) : x(sx), y(sy) { }
    
    s8 x, y;

    std::string to_repr() const {
      return
        std::string("<S8:x=") + std::to_string(x) +
        ",y=" + std::to_string(y) + ">";
    }

    static S8C makeS8CFromDir8(Dir8 d8) {
      switch (d8) {
      case Dir8::NT: return S8C( 0,-1);
      case Dir8::NW: return S8C( 1,-1);
      case Dir8::WT: return S8C( 1, 0);
      case Dir8::SW: return S8C( 1, 1);
      case Dir8::ST: return S8C( 0, 1);
      case Dir8::SE: return S8C(-1, 1);
      case Dir8::ET: return S8C(-1, 0);
      case Dir8::NE: return S8C(-1,-1);
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }

  };

  struct S8CRange {
    S8C start, end;

    u32 area() const {
      return
        (end.x - start.x +1) *
        (end.y - start.y +1);
    }
  };
}
