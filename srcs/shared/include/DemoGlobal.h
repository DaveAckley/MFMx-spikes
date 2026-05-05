#pragma once         /* -*- C++ -*- */

#include "itype.h"
#include "UxC.h" // For U32C
#include "Fail.h"
#include "S32C.h"

namespace MFM::DG {
  /*
    NOTE: Since, FOR THIS DEMO, we are NOT AIMING AT INDEFINITE
    SCALABILITY, we can come up with a nominal GLOBAL scale and
    layout up front.

    WANT: An 'HD-sized' Movable Feast Machine implementation, running
    a fixed rectangular array of 1,920 columns and 1,080 rows of
    programmable cellular automata 'sites'.

    HAVE: A Tenstorrent QuietBox containing 4 Blackhole chips.

    PLAN: Each chip will be primarily responsible for one quarter of
    the ~2M sites:

    +-----------------+-----------------+
    |Blackhole Chip 0 |Blackhole Chip 1 |  ^
    | 960 x 540 sites | 960 x 540 sites |  .
    |                 |                 |  .
    |                 |                 |  .
    |                 |                 |  .
    +-----------------+-----------------+ 1080
    |Blackhole Chip 2 |Blackhole Chip 3 | sites
    | 960 x 540 sites | 960 x 540 sites |  .
    |                 |                 |  .
    |                 |                 |  .
    |                 |                 |  v
    +-----------------+-----------------+
    <- - - - - - - 1920  - - - - - - ->
    sites

    NOTE: Since the Movable Feast Machine DOES NOT require that each
    CA site receives an identical number of globally
    synchronized events, we focus on the AVERAGE EVENT RATE
    (AER) - the average number of events per site per second.

    WANT: To maximize the AER_HD (AER at HD size), but provide as
    'flat' an event distribution across sites as we can manage.

    HAVE: A Tenstorrent QuietBox containing 4 Blackhole chips.

    PLAN: (1) Use the fast Blackhole Networks-on-Chip to coordinate
    loosely within each Blackhole chip.

    (2) Use the fast Blackhole ethernets to coordinate loosely
    between the four chips in the Blackhole QuietBox.

    (3) All details TFBD: Build it and find them out.
  */

  // Demo Global Coordinate
  using Coord = U32C;   // Names for 4G x 4G sites (ho ho sure)

  // Demo Global Address
  struct Address {    // I mean,
    U16C mT6GridC;    // this T6Grid address,
    U8C mCellNum;     // in the hub of this cell,
    u16 mChipNum;     // on this chip,
    bool mValidAddr;  // believe you me, or don't.

    bool isValid() const { return mValidAddr; }
  };

  static constexpr u32 DEMO_GLOBAL_GRID_WIDTH = 1920u;
  static constexpr u32 DEMO_GLOBAL_GRID_HEIGHT = 1080u;

  static constexpr u32 BLACKHOLE_CHIP_ARRAY_WIDTH = 2u;
  static constexpr u32 BLACKHOLE_CHIP_ARRAY_HEIGHT = 2u;

  static constexpr u32 PER_BLACKHOLE_GRID_WIDTH = DEMO_GLOBAL_GRID_WIDTH / BLACKHOLE_CHIP_ARRAY_WIDTH;
  static constexpr u32 PER_BLACKHOLE_GRID_HEIGHT = DEMO_GLOBAL_GRID_HEIGHT / BLACKHOLE_CHIP_ARRAY_HEIGHT;
  
  static constexpr u32 BLACKHOLE_T6_WIDTH = 14u;
  static constexpr u32 BLACKHOLE_T6_HEIGHT = 10u;

  static constexpr u32 BLACKHOLE_HUBS_WIDTH_3X3 = BLACKHOLE_T6_WIDTH / 3u;
  static constexpr u32 BLACKHOLE_HUBS_HEIGHT_3X3 = BLACKHOLE_T6_HEIGHT / 3u;

  static constexpr u32 BLACKHOLE_HUBS_WIDTH_2X2 = BLACKHOLE_T6_WIDTH / 2u;
  static constexpr u32 BLACKHOLE_HUBS_HEIGHT_2X2 = BLACKHOLE_T6_HEIGHT / 2u;

  static constexpr u32 BLACKHOLE_ACTIVE_HUB_WIDTH = BLACKHOLE_HUBS_WIDTH_2X2;
  static constexpr u32 BLACKHOLE_ACTIVE_HUB_HEIGHT = BLACKHOLE_HUBS_HEIGHT_2X2;

  static constexpr u32 T6GRID_WIDTH =
    (PER_BLACKHOLE_GRID_WIDTH + BLACKHOLE_ACTIVE_HUB_WIDTH - 1u) / BLACKHOLE_ACTIVE_HUB_WIDTH;
  static constexpr u32 T6GRID_HEIGHT =
    (PER_BLACKHOLE_GRID_HEIGHT + BLACKHOLE_ACTIVE_HUB_HEIGHT - 1u) / BLACKHOLE_ACTIVE_HUB_HEIGHT;

  static constexpr U16C getGlobalGridSize() {
    return U16C(DEMO_GLOBAL_GRID_WIDTH,DEMO_GLOBAL_GRID_HEIGHT);
  }
  static constexpr U16C getSingleT6GridSize() { return U16C(T6GRID_WIDTH,T6GRID_HEIGHT); }
  static constexpr U16C getChipPositionInArray(u32 chipnumber) {
    return U16C(chipnumber % BLACKHOLE_CHIP_ARRAY_WIDTH, chipnumber / BLACKHOLE_CHIP_ARRAY_WIDTH);
  }
  static constexpr u32 getChipNumberFromArrayPosition(U32C arrayPosition) {
    return arrayPosition.y * BLACKHOLE_CHIP_ARRAY_WIDTH + arrayPosition.x;
  }

  static constexpr bool isValidChipCoord(U32C chipAddr) {
    return
      chipAddr.x < BLACKHOLE_ACTIVE_HUB_WIDTH &&
      chipAddr.y < BLACKHOLE_ACTIVE_HUB_HEIGHT;
  }
  static constexpr U16C getChipSites() {
    return U16C(PER_BLACKHOLE_GRID_WIDTH, PER_BLACKHOLE_GRID_HEIGHT);
  }
  static constexpr U16C getChipOrigin(u32 chipnumber) {
    return getChipPositionInArray(chipnumber) * getChipSites();
  }

  static U16C getTLBIOrigin(u32 tlbi,U8C cellstride) {
    U8C ct6c = U8C::makeCT6CoordFromTLBI(tlbi)/cellstride;
    return U16C(ct6c.x,ct6c.y)*getSingleT6GridSize();
  }

  static Coord mapS32CToCoord(S32C center) {
    MFM_API_ASSERT(center.x + (s32) DEMO_GLOBAL_GRID_WIDTH/2 >= 0 &&
                   center.y + (s32) DEMO_GLOBAL_GRID_HEIGHT/2 >= 0 , ILLEGAL_ARGUMENT);
    return Coord((u32) center.x + DEMO_GLOBAL_GRID_WIDTH/2,
                 (u32) center.y + DEMO_GLOBAL_GRID_HEIGHT/2);
  }

  static Address mapCoordToAddress(const Coord dgc) {
    Address ret;
    ret.mValidAddr = false;
    if (dgc.x >= DEMO_GLOBAL_GRID_WIDTH) return ret;
    if (dgc.y >= DEMO_GLOBAL_GRID_HEIGHT) return ret;
    U16C chipsites = getChipSites();
    U32C cs32(chipsites.x,chipsites.y);
    U32C chipAddr = dgc/cs32;
    if (!isValidChipCoord(chipAddr)) return ret;
    ret.mChipNum = getChipNumberFromArrayPosition(chipAddr);

    U16C corigin = getChipOrigin(ret.mChipNum);
    U16C chipSiteC(dgc.x - corigin.x, dgc.y - corigin.y);
    U16C t6size = getSingleT6GridSize();
    U16C cn16 = chipSiteC / t6size;
    U16C ca16 = chipSiteC % t6size;

    ret.mCellNum = U8C(cn16.x, cn16.y);
    ret.mT6GridC = ca16;
    ret.mValidAddr = true;
    return ret;
  }

  static bool mapAddressToCoord(const Address & qba, Coord & qbc) {
    FAIL(INCOMPLETE_CODE);
    return false; 
  }

}

