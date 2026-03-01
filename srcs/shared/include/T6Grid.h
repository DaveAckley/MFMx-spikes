#pragma once  /* -*- C++ -*- */
#include "itype.h"

#include "P4Atom.h"
#include "MDist.h"
#include "U8C.h"
#include "S8C.h"

namespace MFM {
  static constexpr u32 NOMINAL_GRID_WIDTH = 1920u/* *2u/3u */;
  static constexpr u32 NOMINAL_GRID_HEIGHT = 1080u/* *2u/3u */;

  static constexpr u32 BLACKHOLE_T6_WIDTH = 14u;
  static constexpr u32 BLACKHOLE_T6_HEIGHT = 10u;
  
  static constexpr u32 T6GRID_WIDTH = (NOMINAL_GRID_WIDTH + BLACKHOLE_T6_WIDTH - 1u)/BLACKHOLE_T6_WIDTH;
  static constexpr u32 T6GRID_HEIGHT = (NOMINAL_GRID_HEIGHT + BLACKHOLE_T6_HEIGHT - 1u)/BLACKHOLE_T6_HEIGHT;

  struct T6Grid {
    P4Atom mT6Grid[T6GRID_WIDTH][T6GRID_HEIGHT];

    P4Atom & getAtom(U8C c) {
      MFM_API_ASSERT(c.x < T6GRID_WIDTH && c.y < T6GRID_HEIGHT, ILLEGAL_ARGUMENT);
      return mT6Grid[c.x][c.y];
    }
    
    /** coord of site in this T6Grid that is closest to c4 */
    static U8C tileCoordOfCorner(Corner4 c4) {
      switch (c4) {
      case C4_SE: return U8C(T6GRID_WIDTH-1, T6GRID_HEIGHT-1);
      case C4_SW: return U8C(0u, T6GRID_HEIGHT-1);
      case C4_NW: return U8C(0u, 0u);
      case C4_NE: return U8C(T6GRID_WIDTH-1, 0u);
      }
      return U8C(U8_MAX,U8_MAX); // try to blow things up in lieu for paying for a FAIL(UNREACHABLE_CODE);
    }

    /** the extended tile coord, for the c4 corner,
        corresponding to corner coord (0,0) of a ring
    */
    static S8C tileCoordCornerOrigin(Corner4 c4) {
      switch (c4) {
      case C4_SE: return S8C(T6GRID_WIDTH, T6GRID_HEIGHT);
      case C4_SW: return S8C(0, T6GRID_HEIGHT);
      case C4_NW: return S8C(0, 0);
      case C4_NE: return S8C(T6GRID_WIDTH, 0);
      }
      return S8C(S8_MIN,S8_MIN); // try to blow things up in lieu for paying for a FAIL(UNREACHABLE_CODE);
    }

    static bool coordInTile(S8C tc) {
      return
        tc.x >= 0 && tc.x < (s32) T6GRID_WIDTH &&  
        tc.y >= 0 && tc.y < (s32) T6GRID_HEIGHT;
    }

    static bool coordInTile(U8C tc) {
      return tc.x < T6GRID_WIDTH && tc.y < T6GRID_HEIGHT;
    }

    static S8C tileCoordToCornerCoord(U8C tc, Corner4 c4) {
      S8C cc = tileCoordCornerOrigin(c4);
      return cc - S8C(tc);
    }

    static S8C cornerCoordToExtendedTileCoord(S8C cc, Corner4 c4) {
      S8C tc = tileCoordCornerOrigin(c4);
      return cc + tc;
    }

    static bool cornerCoordToTileCoordIfAny(S8C cc, Corner4 c4, U8C & tc) {
      S8C cc2 = cornerCoordToExtendedTileCoord(cc,c4);
      if (!coordInTile(cc2)) return false;
      tc.x = (u8) cc2.x;
      tc.y = (u8) cc2.y;
      return true;
    }
  };

  extern T6Grid theT6Grid[1];
}
