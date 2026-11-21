#pragma once         /* -*- C++ -*- */

#include "itype.h"
#include "utils.h" // For XSTR_MACRO
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

  static bool isValidDGC(U16C coord) {
    return
      coord.x < DEMO_GLOBAL_GRID_WIDTH &&
      coord.y < DEMO_GLOBAL_GRID_HEIGHT;
  }

  static constexpr u32 BLACKHOLE_CHIP_ARRAY_WIDTH = 2u;
  static constexpr u32 BLACKHOLE_CHIP_ARRAY_HEIGHT = 2u;

  static constexpr u32 PER_BLACKHOLE_NAIVE_GRID_WIDTH = DEMO_GLOBAL_GRID_WIDTH / BLACKHOLE_CHIP_ARRAY_WIDTH;
  static constexpr u32 PER_BLACKHOLE_NAIVE_GRID_HEIGHT = DEMO_GLOBAL_GRID_HEIGHT / BLACKHOLE_CHIP_ARRAY_HEIGHT;
  
  static constexpr u32 QUIETBOX_T6_WIDTH = 14u;
  static constexpr u32 QUIETBOX_T6_HEIGHT = 10u;

  static constexpr u32 BLACKHOLE_HUBS_WIDTH_3X3 = QUIETBOX_T6_WIDTH / 3u;
  static constexpr u32 BLACKHOLE_HUBS_HEIGHT_3X3 = QUIETBOX_T6_HEIGHT / 3u;

  static constexpr u32 BLACKHOLE_HUBS_WIDTH_2X2 = QUIETBOX_T6_WIDTH / 2u;
  static constexpr u32 BLACKHOLE_HUBS_HEIGHT_2X2 = QUIETBOX_T6_HEIGHT / 2u;

  static constexpr u32 BLACKHOLE_ACTIVE_HUB_WIDTH = BLACKHOLE_HUBS_WIDTH_2X2;
  static constexpr u32 BLACKHOLE_ACTIVE_HUB_HEIGHT = BLACKHOLE_HUBS_HEIGHT_2X2;

  /* Hub SELF size is the FLOOR of chip size over hubs, so the hubs
     always FIT in the global grid: 960/7 -> 137 (7*137 = 959 per chip,
     1918 across the box, 2 blank columns at the right of the 1920 image),
     540/5 -> 108 exactly.  (WAS ceiling: 138, which overran the image by
     12 columns and needed a display crop at the right.) */
  static constexpr u32 T6GRID_BASE_WIDTH =
    PER_BLACKHOLE_NAIVE_GRID_WIDTH / BLACKHOLE_ACTIVE_HUB_WIDTH;
  static constexpr u32 T6GRID_BASE_HEIGHT =
    PER_BLACKHOLE_NAIVE_GRID_HEIGHT / BLACKHOLE_ACTIVE_HUB_HEIGHT;

  static constexpr u32 T6GRID_OVERLAP_WIDTH = 20; //XXX WAS: T6GRID_BASE_WIDTH/11;
  static constexpr u32 T6GRID_OVERLAP_HEIGHT = 20; //XXX WAS: T6GRID_BASE_HEIGHT/11;

  static constexpr u32 T6GRID_WIDTH = T6GRID_BASE_WIDTH + 2*T6GRID_OVERLAP_WIDTH;
  static constexpr u32 T6GRID_HEIGHT = T6GRID_BASE_HEIGHT + 2*T6GRID_OVERLAP_HEIGHT;

  static constexpr u32 PER_BLACKHOLE_GRID_WIDTH = BLACKHOLE_ACTIVE_HUB_WIDTH * T6GRID_BASE_WIDTH;
  static constexpr u32 PER_BLACKHOLE_GRID_HEIGHT = BLACKHOLE_ACTIVE_HUB_HEIGHT * T6GRID_BASE_HEIGHT;

  /* GEOMETRY GUARDS.  Hub sizes are derived above, never repeated as
     literals elsewhere in C++ (Python: HubGeometry.py, checked against
     this file by scripts/hubgeom/hubgeom_check.sh).  These catch a
     change to the inputs that breaks an assumption made downstream. */
  static_assert(PER_BLACKHOLE_GRID_WIDTH * BLACKHOLE_CHIP_ARRAY_WIDTH <=
                DEMO_GLOBAL_GRID_WIDTH,
                "hubs must fit in the global grid width");
  static_assert(PER_BLACKHOLE_GRID_HEIGHT * BLACKHOLE_CHIP_ARRAY_HEIGHT <=
                DEMO_GLOBAL_GRID_HEIGHT,
                "hubs must fit in the global grid height");
  static_assert(PER_BLACKHOLE_GRID_WIDTH * BLACKHOLE_CHIP_ARRAY_WIDTH +
                BLACKHOLE_ACTIVE_HUB_WIDTH * BLACKHOLE_CHIP_ARRAY_WIDTH >
                DEMO_GLOBAL_GRID_WIDTH,
                "hub width is smaller than needed: a whole spare column "
                "per hub would fit");
  static_assert(PER_BLACKHOLE_GRID_HEIGHT * BLACKHOLE_CHIP_ARRAY_HEIGHT +
                BLACKHOLE_ACTIVE_HUB_HEIGHT * BLACKHOLE_CHIP_ARRAY_HEIGHT >
                DEMO_GLOBAL_GRID_HEIGHT,
                "hub height is smaller than needed");
  static_assert(T6GRID_WIDTH <= 255u && T6GRID_HEIGHT <= 255u,
                "NgbCacheMgr stores T6Grid sizes in u8 enums");
  static_assert(2u * T6GRID_OVERLAP_WIDTH < T6GRID_BASE_WIDTH &&
                2u * T6GRID_OVERLAP_HEIGHT < T6GRID_BASE_HEIGHT,
                "cache rings must leave room for a SELF region");

  static std::string dumpConstants() {
    std::string ret = "";
#define XX(C) ret += std::string(XSTR_MACRO(C)) + " = " + std::to_string(C) + "\n"
    XX(DEMO_GLOBAL_GRID_WIDTH);
    XX(DEMO_GLOBAL_GRID_HEIGHT);

    XX(BLACKHOLE_CHIP_ARRAY_WIDTH);
    XX(BLACKHOLE_CHIP_ARRAY_HEIGHT);

    XX(PER_BLACKHOLE_NAIVE_GRID_WIDTH);
    XX(PER_BLACKHOLE_NAIVE_GRID_HEIGHT);
  
    XX(QUIETBOX_T6_WIDTH);
    XX(QUIETBOX_T6_HEIGHT);

    XX(BLACKHOLE_HUBS_WIDTH_3X3);
    XX(BLACKHOLE_HUBS_HEIGHT_3X3);

    XX(BLACKHOLE_HUBS_WIDTH_2X2);
    XX(BLACKHOLE_HUBS_HEIGHT_2X2);

    XX(BLACKHOLE_ACTIVE_HUB_WIDTH);
    XX(BLACKHOLE_ACTIVE_HUB_HEIGHT);

    XX(T6GRID_BASE_WIDTH);
    XX(T6GRID_BASE_HEIGHT);

    XX(T6GRID_OVERLAP_WIDTH);
    XX(T6GRID_OVERLAP_HEIGHT);

    XX(T6GRID_WIDTH);
    XX(T6GRID_HEIGHT);

    XX(PER_BLACKHOLE_GRID_WIDTH);
    XX(PER_BLACKHOLE_GRID_HEIGHT);

#undef XX    
    return ret;
  }

  static constexpr U16C getGlobalGridSize() {
    return U16C(DEMO_GLOBAL_GRID_WIDTH,DEMO_GLOBAL_GRID_HEIGHT);
  }
  /* DISPLAY ONLY.  A hub's T6Grid is drawn into the 1920x1080 image with
     its N and W cache rings cropped away, so a hub's SELF sites land on
     its own global site coordinates: gridc is the hub origin plus the
     T6Grid coord, pixel = gridc - getGlobalGridOffset().  Rings of the first hub row/column (permanent
     array edges) fall off the top/left.  A full QuietBox is
     QUIETBOX_T6_WIDTH x T6GRID_BASE_WIDTH = 1918 sites wide, so the
     last 2 image columns stay blank (at the old base width 138 it was
     1932 and the excess fell off the right).  The hubs are not told
     anything: they run all their sites.  False if the site is
     cropped away at the top/left (the caller skips it); the image's own
     setPixel drops anything past the right/bottom. */
  static bool gridCoordToPixel(U16C gridc, U16C & pix) {
    if (gridc.x < T6GRID_OVERLAP_WIDTH || gridc.y < T6GRID_OVERLAP_HEIGHT)
      return false;
    pix = U16C((u16) (gridc.x - T6GRID_OVERLAP_WIDTH),
               (u16) (gridc.y - T6GRID_OVERLAP_HEIGHT));
    return true;
  }
  static constexpr U16C getGlobalGridOffset() { return U16C(T6GRID_OVERLAP_WIDTH,T6GRID_OVERLAP_HEIGHT); }
  static constexpr U16C getSingleT6GridBaseSize() { return U16C(T6GRID_BASE_WIDTH,T6GRID_BASE_HEIGHT); } //< EXCLUDING caches
  static constexpr U16C getSingleT6GridSize() { return U16C(T6GRID_WIDTH,T6GRID_HEIGHT); } //< including caches
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

  /* HUBS ARE GLOBALLY CONTIGUOUS.  A chip is BLACKHOLE_ACTIVE_HUB_WIDTH x
     BLACKHOLE_ACTIVE_HUB_HEIGHT hubs (7 x 5) of T6GRID_BASE_WIDTH x
     T6GRID_BASE_HEIGHT sites, and chip origins are multiples of that, so hub columns and
     rows simply continue from one chip to the next.  The hub grid is
     divided into 2x2 supercells, and each hub's place in its supercell
     must be counted in GLOBAL hub coordinates.  Counting per chip is
     wrong whenever a chip has an odd number of hubs along an axis
     (7 and 5 here): the first hub column of chip 1 would then have the
     same parity as the last of chip 0, and two adjacent hubs could both
     be supercell leader. */

  /// Global hub coordinates of a chip's first hub (hub 0,0 of that chip)
  static constexpr U8C getChipHubOrigin(u32 chipnumber) {
    return U8C((u8) (getChipPositionInArray(chipnumber).x
                     * BLACKHOLE_ACTIVE_HUB_WIDTH),
               (u8) (getChipPositionInArray(chipnumber).y
                     * BLACKHOLE_ACTIVE_HUB_HEIGHT));
  }

  /** Position (0..1, 0..1) within its 2x2 supercell of the hub at hub
      coordinates (hubx, huby) of the chip 'chipnumber'.  The hub
      coordinates are relative to that chip and may lie outside it
      (negative, or past its last hub) to name a neighbor on another
      chip. */
  static constexpr U8C getHubSuperCellCoord(u32 chipnumber,
                                            s32 hubx,
                                            s32 huby) {
    return U8C((u8) ((((s32) getChipHubOrigin(chipnumber).x) + hubx) & 1),
               (u8) ((((s32) getChipHubOrigin(chipnumber).y) + huby) & 1));
  }

  static U16C getTLBIOrigin(u32 tlbi,U8C cellstride) {
    U8C ct6c = U8C::makeCT6CoordFromTLBI(tlbi)/cellstride;
    return U16C(ct6c.x,ct6c.y)*getSingleT6GridBaseSize();
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
    U16C t6size = getSingleT6GridBaseSize();
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

