#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include <string>
#include "MDist.h"
#include "Point.h" // for SPoint

namespace MFM {
  struct U8C; // FORWARD

  struct S8C {
    S8C() : x(0) , y(0) { }
    S8C(const SPoint sp) : x(sp.GetX()), y(sp.GetY()) { }
    S8C(s32 sx, s32 sy) : x(sx), y(sy) { }
    S8C(U8C u) ;
    
    s8 x, y;

    S8C operator+(const S8C other) const { return S8C(x+other.x,y+other.y); }
    S8C operator-(const S8C other) const { return S8C(x-other.x,y-other.y); }

    u16 length() const {
      u16 l = 0;
      l += (x<0) ? -x: x;
      l += (y<0) ? -y: y;
      return l;
    }

    std::string to_string() const {
      return
        std::string("S8C(") + std::to_string(x) +
        "," + std::to_string(y) + ")";
    }

    std::string to_repr() const {
      return
        std::string("<S8:x=") + std::to_string(x) +
        ",y=" + std::to_string(y) + ">";
    }

    static S8C makeS8CFromDir8(Dir8 d8) {
      switch (d8) {
      case D8_NT: return S8C( 0,-1);
      case D8_NW: return S8C( 1,-1);
      case D8_WT: return S8C( 1, 0);
      case D8_SW: return S8C( 1, 1);
      case D8_ST: return S8C( 0, 1);
      case D8_SE: return S8C(-1, 1);
      case D8_ET: return S8C(-1, 0);
      case D8_NE: return S8C(-1,-1);
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
