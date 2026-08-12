#pragma once  /* -*- C++ -*- */
#include "itype.h"

#include "DemoGlobal.h"
#include "P4Atom.h"
#include "MDist.h"
#include "UxC.h" // For U16C
#include "S16C.h"
#include "Debug.h"

namespace MFM {

  static constexpr bool isValidT6GridC(U16C c) { return c.x < DG::T6GRID_WIDTH && c.y < DG::T6GRID_HEIGHT; }

  struct T6Grid {
    static constexpr u32 FULL_WIDTH = DG::T6GRID_WIDTH;
    static constexpr u32 FULL_HEIGHT = DG::T6GRID_HEIGHT;
    static constexpr U16C SELF_ORIGIN = { DG::T6GRID_OVERLAP_WIDTH, DG::T6GRID_OVERLAP_HEIGHT };
    static constexpr U16C SELF_MAX = { FULL_WIDTH - DG::T6GRID_OVERLAP_WIDTH, FULL_HEIGHT - DG::T6GRID_OVERLAP_HEIGHT };

    P4Atom mT6Grid[FULL_WIDTH][FULL_HEIGHT]; //< canonical atoms for this T6
    u32 mTotalChanges;          //< will wrap eventually but do we care?

    static U16CRange getCacheRange(Dir8 d8) {
      U16CRange ret;
      switch (d8) {
      case D8_NT: ret.init({SELF_ORIGIN.x,0},{SELF_MAX.x,SELF_ORIGIN.y-1}); break;
      case D8_ST: ret.init({SELF_ORIGIN.x,SELF_MAX.y},{SELF_MAX.x,FULL_HEIGHT-1}); break;
      case D8_ET: ret.init({SELF_MAX.x,SELF_ORIGIN.y-1},{FULL_WIDTH-1,SELF_MAX.y-1}); break;
      case D8_WT: ret.init({0,SELF_ORIGIN.y},{SELF_ORIGIN.x-1,SELF_MAX.y-1}); break;
      default: FAIL(INCOMPLETE_CODE);
      }
      return ret;
    }

    static u32 getByteOffsetToT6GridAtom(const U16C c) {
      HBASSERT_EQ(isValidT6GridC(c),true);
      return offsetof(T6Grid,mT6Grid) + sizeof(P4Atom)*(c.x*DG::T6GRID_HEIGHT+c.y); 
    }

    u32 getTotalChanges() const { return mTotalChanges; }

    static U16C getGridSize() { return U16C(FULL_WIDTH,FULL_HEIGHT); }

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    P4Atom getAtom(U16C c) const {
      HBASSERT_EQ(isValidT6GridC(c),true);
      return mT6Grid[c.x][c.y];
    }

    P4Atom getAtomOrInaccessible(U16C c) const {
      if (isValidT6GridC(c)) return getAtom(c);
      return P4Atom::makeInaccessibleAtom();
    }

    bool setAtom(U16C c, const P4Atom newval) {
      HBASSERT_EQ(isValidT6GridC(c),true);
      if (mT6Grid[c.x][c.y] == newval) return false;
      mT6Grid[c.x][c.y] = newval;
      ++mTotalChanges;
      return true;
    }

    bool setAtomOrDrop(U16C c, const P4Atom newval) {
      if (!isValidT6GridC(c)) return false;
      return setAtom(c,newval);
    }

    /** coord of site in this T6Grid that is closest to c4 */
    static U16C tileCoordOfCorner(Corner4 c4) {
      switch (c4) {
      case C4_SE: return U16C(DG::T6GRID_WIDTH-1, DG::T6GRID_HEIGHT-1);
      case C4_SW: return U16C(0u, DG::T6GRID_HEIGHT-1);
      case C4_NW: return U16C(0u, 0u);
      case C4_NE: return U16C(DG::T6GRID_WIDTH-1, 0u);
      }
      return U16C(U8_MAX,U8_MAX); // try to blow things up in lieu of paying for a FAIL(UNREACHABLE_CODE);
    }

    /** the extended tile coord, for the c4 corner,
        corresponding to corner coord (0,0) of a ring
    */
    static S16C tileCoordCornerOrigin(Corner4 c4) {
      switch (c4) {
      case C4_SE: return S16C(DG::T6GRID_WIDTH, DG::T6GRID_HEIGHT);
      case C4_SW: return S16C(0, DG::T6GRID_HEIGHT);
      case C4_NW: return S16C(0, 0);
      case C4_NE: return S16C(DG::T6GRID_WIDTH, 0);
      }
      return S16C(S8_MIN,S8_MIN); // try to blow things up in lieu for paying for a FAIL(UNREACHABLE_CODE);
    }

    static bool coordInTile(S16C tc) {
      return
        tc.x >= 0 && tc.x < (s32) DG::T6GRID_WIDTH &&
        tc.y >= 0 && tc.y < (s32) DG::T6GRID_HEIGHT;
    }

    static bool coordInTile(U16C tc) {
      return tc.x < DG::T6GRID_WIDTH && tc.y < DG::T6GRID_HEIGHT;
    }

    static S16C tileCoordToCornerCoord(U16C tc, Corner4 c4) {
      S16C cc = tileCoordCornerOrigin(c4);
      return cc - S16C(tc);
    }

    static S16C cornerCoordToExtendedTileCoord(S16C cc, Corner4 c4) {
      S16C tc = tileCoordCornerOrigin(c4);
      return cc + tc;
    }

    static bool cornerCoordToTileCoordIfAny(S16C cc, Corner4 c4, U16C & tc) {
      S16C cc2 = cornerCoordToExtendedTileCoord(cc,c4);
      if (!coordInTile(cc2)) return false;
      tc.x = (u8) cc2.x;
      tc.y = (u8) cc2.y;
      return true;
    }
  };

  extern T6Grid theT6Grid[1];
}
