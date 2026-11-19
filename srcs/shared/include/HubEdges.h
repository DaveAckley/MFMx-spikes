#pragma once  /* -*- C++ -*- */
/* Which neighbor hubs exist, and so which sides of a hub are PERMANENT
   array edges (as opposed to a temporary dead band left by a caching
   policy).  Needs no HostBlock field and no HostFlags bit: the hub
   already knows its chip's live eth-seams (HostBlock::getEthSeamMask).

   A neighbor hub exists if it is on this chip, or across a live seam
   (a Dir4 neighbor, or the two-hop corner crossing for a diagonal).
   A Dir4 side whose neighbor does not exist is a permanent edge: its
   10-wide cache ring is INACCESSIBLE (never written, never exchanged,
   EMPTY from initialization), and event centers may run right up to the
   last real site on that side.  See OPEN_ISSUES.md, "PERMANENT array
   edges".  Host-testable: scripts/hubedges/hubedges_test.cpp.
*/
#include "itype.h"
#include "MDist.h"
#include "UxC.h"
#include "S8C.h"
#include "EthSeam.h"

namespace MFM {

  struct HubEdges {
    enum : u8 {
      PE_N = 1u << D4_N,        //< no hub to our north: permanent north edge
      PE_W = 1u << D4_W,
      PE_S = 1u << D4_S,
      PE_E = 1u << D4_E,
    };

    /* Is the hub in direction d8 from the hub at CT6 tile ourCT6C on this
       chip?  (A hub is the (1,1) tile of a 2x2 cell of CT6 tiles.) */
    static bool neighborOnChip(Dir8 d8, U8C ourCT6C) {
      const U8C uct6_per_ureg(2,2);
      S8C d8nREGV = S8C::makeS8CFromDir8(d8);
      S8C d8nCT6V = d8nREGV * S8C(uct6_per_ureg);
      S8C d8nCT6C = S8C(ourCT6C) + d8nCT6V;
      U8C ud8nCT6C;
      return d8nCT6C.toU8C(ud8nCT6C) &&
        U8C::makeNoC0CoordFromCT6Coord(ud8nCT6C) != U8C::UxC_MAX_VAL;
    }

    /* Does that neighbor hub exist: on this chip, or across a live seam? */
    static bool neighborExists(Dir8 d8,
                               const EthSeam::SeamTable & seams,
                               U8C ourCT6C) {
      if (neighborOnChip(d8, ourCT6C)) return true;
      if ((d8 & 1u) != 0u) {
        EthSeam::DiagSend ds;
        return EthSeam::diagSend(seams, d8, ourCT6C.x, ourCT6C.y,
                                 EthSeam::CELL_C2X2, ds);
      }
      EthSeam::DestDir4 dest;
      return EthSeam::destDir4(seams, dir8ToDir4CW(d8), ourCT6C.x,
                               ourCT6C.y, EthSeam::CELL_C2X2, dest);
    }

    /* The PE_N|PE_W|PE_S|PE_E bits of the sides with no neighbor. */
    static u8 permanentEdges(const EthSeam::SeamTable & seams, U8C ourCT6C) {
      u8 ret = 0u;
      for (u32 d4 = D4_N; d4 <= D4_E; ++d4)
        if (!neighborExists(dir4ToDir8((Dir4) d4), seams, ourCT6C))
          ret |= (u8) (1u << d4);
      return ret;
    }
  };
}
