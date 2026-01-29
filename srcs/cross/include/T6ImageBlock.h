#pragma once  /* -*- C++ -*- */
#include "itype.h"

#include "ImageBlock.h"
#include "MDist.h"
#include "U8C.h"
#include "S8C.h"

namespace MFM {

  struct T6Neighbor {
    T6Neighbor() { }

    bool init (U8C ournoc0c, S8C ngbc) {
      mNoC0Ngb.reset();         // Assume blown
      mNgbCellCoord = ngbc;
      mNoC0Us = ournoc0c;
      U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(ournoc0c);
      if (!U8C::onBoardCT6Coord(usct6)) return false;
      U8C ngbct6 = usct6 + ngbc;
      if (!U8C::onBoardCT6Coord(ngbct6)) return false;
      mNoC0Ngb = U8C::makeNoC0CoordFromCT6Coord(ngbct6);
      return true;
    }

    bool isValid() const {
      return U8C::isNoC0CoordAT6(mNoC0Ngb);
    }

    S8C mNgbCellCoord;
    U8C mNoC0Us;
    U8C mNoC0Ngb;
  };

  struct T6ImageBlock {
    ImageBlockHeader readHeader(T6Neighbor & ctn) ;
    
#if 0
XXXX CUT BELOW?

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
#endif
  };
}
