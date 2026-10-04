#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include <string>
#include "MDist.h"
#include "Point.h" // for SPoint
#include "UxC.h" // for U16C

namespace MFM {
  //  struct U16C; // FORWARD

  struct S16C {
    S16C() = default;
    S16C(const SPoint sp) : x(sp.GetX()), y(sp.GetY()) { }
    S16C(s32 sx, s32 sy) : x(sx), y(sy) { }
    S16C(U16C u) ;
    
    s16 x, y;

    S16C operator+(const S16C other) const { return S16C(x+other.x,y+other.y); }
    S16C operator-(const S16C other) const { return S16C(x-other.x,y-other.y); }

    bool operator==(const S16C & other) const { return x==other.x && y==other.y; }
    bool operator!=(const S16C & other) const { return !(*this == other); }
    S16C operator*(const S16C & other) const { return S16C(x * other.x, y * other.y); }
    S16C operator/(const S16C & other) const { return S16C(x / other.x, y / other.y); }
    S16C operator%(const S16C & other) const { return S16C(x % other.x, y % other.y); }

    u32 length() const {
      u32 l = 0;
      l += (x<0) ? -x: x;
      l += (y<0) ? -y: y;
      return l;
    }

    std::string to_string() const {
      return
        std::string("S16C(") + std::to_string(x) +
        "," + std::to_string(y) + ")";
    }

    std::string to_repr() const {
      return
        std::string("<S16:x=") + std::to_string(x) +
        ",y=" + std::to_string(y) + ">";
    }

    static S16C makeS16CFromDir8(Dir8 d8) {
      switch (d8) {
      case D8_NT: return S16C( 0,-1);
      case D8_NW: return S16C( 1,-1);
      case D8_WT: return S16C( 1, 0);
      case D8_SW: return S16C( 1, 1);
      case D8_ST: return S16C( 0, 1);
      case D8_SE: return S16C(-1, 1);
      case D8_ET: return S16C(-1, 0);
      case D8_NE: return S16C(-1,-1);
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }

  };
}

#include "UxC.h" // for U16C

namespace MFM {
  inline S16C::S16C(U16C u) {
    MFM_API_ASSERT(u.x <= S16_MAX && u.y <= S16_MAX, ILLEGAL_ARGUMENT);
    x = (s16) u.x;
    y = (s16) u.y;
  }
}

