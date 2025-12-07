#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include <string>
#include "MDist.h"

namespace MFM {
  struct S8C; // FORWARD


  struct U8C {
    u8 x, y;

    U8C() : x(0), y(0) { }
    U8C(u8 ax, u8 ay) : x(ax), y(ay) { }

    U8C operator+(const S8C & s8) const ;
    bool addTo(const S8C & s8) ;

    std::string to_repr() const {
      return
        std::string("<U8:x=") + std::to_string(x) +
        ",y=" + std::to_string(y) + ">";
    }

    static U8C makeU8CRawT6CoordFromTLBI(uint32_t tlbidx) {
      U8C ret;
      ret.x = tlbidx%14u;
      ret.y = tlbidx/14u;
      return ret;
    }

    static U8C makeU8CNocCoordFromTLBI(uint32_t tlbidx) {
      U8C ret = makeU8CRawT6CoordFromTLBI(tlbidx);
      ret.x++;
      if (ret.x > 7u) ret.x += 2u;
      ret.y += 2u;
      return ret;
    }

    static u32 makeTLBIFromRawU8CCoord(U8C c) {
      u32 ret = c.y*14u + c.x;
      return ret;
    }

    static u32 makeU8CTLBIFromNocU8CCoord(U8C c) {
      c.y -= 2u;
      if (c.x > 9u) c.x -= 2u;
      c.x--;
      return makeTLBIFromRawU8CCoord(c);
    }

    static u32 makeNoCNodeIdFromU8CCoord(U8C c) {
      return (u32) (((c.y&0x3f)<<6)|(c.x&0x3f));      
    }

    static U8C makeU8CFromNoCNodeId(u32 nodeid) {
      U8C ret;
      ret.x = nodeid&0x3f;
      ret.y = (nodeid>>6)&0x3f;
      return ret;
    }
  };

  struct U8CRange {
    U8C start, end;

    u32 area() const {
      return
        (end.x - start.x +1) *
        (end.y - start.y +1);
    }
  };
}
