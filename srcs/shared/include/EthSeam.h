#pragma once /* -*- C++ -*- */
/** EthSeam.h -- ONE copy of the eth-seam layout and channel-assignment rule, shared by the T6 images, the E1 image,
    and the host tools (srcs/host/tools/ERISC_SEAM_DESIGN.md sections 4-7, 13).  Header-only, no libc, no dynamic memory.

    PART 1  E1 / ring layout.  These numbers are what e1fwd13..16 and ethfwd14..20 use TODAY (the tools still carry their own copies;
            ethseam_test.cpp static-asserts that this header matches them).  Change a value here only together with the E1 image.
    PART 2  Which (ETH tile, channel) carries which destination storage: the "balanced dealing" rule.  GEOMETRY-INDEPENDENT: it
            takes the number of edge hubs E on a seam (a function of the cell size, see edgeHubs*) and the number of live links L.
            The same function on both ends of a seam, host and T6 alike, so no seam table has to be delivered, only the live link
            list (<= 4 ETH x coordinates per seam).

    TERMS.  Edge index e: position of a hub along a card edge, counted from the north end (east/west seams) or the west end
    (north/south seams), 0..E-1.  Slot: which directed storage of edge hub e crosses the seam: SLOT_CARDINAL (ihub -> ihub across),
    SLOT_DIAG_LO / SLOT_DIAG_HI (a diagonal toward the neighbor at edge index e-1 / e+1; whether that crossing is the first hop
    (ihub -> chub) or the CHUB forwarder's second hop (chub -> ihub corner) depends on the seam direction and is T6-side knowledge,
    InterHub.cpp; here it only matters that it is ONE directed storage needing ONE channel).  A channel = one OUT ring + one IN ring
    of RING_CARS cars on the E1 of one live link; the two directions of a seam share channel numbers (A.OUT[c] -> B.IN[c], B.OUT[c] -> A.IN[c]). */

#include "itype.h"

namespace MFM {
  namespace EthSeam {

    //// PART 1: layout ////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef ETHSEAM_NCH_MAX
    static constexpr u32 NCH_MAX = ETHSEAM_NCH_MAX; //< channels per E1 tile; the images built so far use 8
#else
    static constexpr u32 NCH_MAX = 8u;
#endif
    static constexpr u32 CAR_BYTES = 0x1040u;       //< one car (InterHubBlock) 4160 B
    static constexpr u32 RING_CARS = 4u;            //< cars per ring (InterHubStorage)
    static constexpr u32 RING_BYTES = RING_CARS * CAR_BYTES;     //< 0x4100
    static constexpr u32 CHUNK_BYTES = 1456u;       //< bytes per TT-link packet (E1 splits cars into these)
    static constexpr u32 E1_CODE = 0x4000u;         //< E1 image load address in ETH L1
    static constexpr u32 CTL_BASE = 0x8000u;        //< E1 control block
    static constexpr u32 CHANNEL_BLOCK0 = CTL_BASE + 0x100u;
    static constexpr u32 CHANNEL_BLOCK_STRIDE = 0x80u;
    static constexpr u32 CTL_WINDOW = ((0x100u + NCH_MAX * CHANNEL_BLOCK_STRIDE) <= 0x600u) ? 0x600u
      : ((0x100u + NCH_MAX * CHANNEL_BLOCK_STRIDE + 0xffu) & ~0xffu);   //< bytes the host may write from CTL_BASE
    static constexpr u32 OUT_RING0 = 0x10000u;      //< E1 OUT ring of channel 0 (the T6 NoC-writes cars here)
    static constexpr u32 IN_RING0 = OUT_RING0 + NCH_MAX * RING_BYTES;  //< E1 IN ring of channel 0 (the peer E1 writes here)
    static constexpr u32 ETH_L1_SYSENG_BASE = 0x70000u;                //< top 64 KiB of ETH L1 belongs to base firmware
    static_assert(IN_RING0 + NCH_MAX * RING_BYTES <= ETH_L1_SYSENG_BASE, "rings must stay below the syseng region");
    static_assert(CHANNEL_BLOCK0 + NCH_MAX * CHANNEL_BLOCK_STRIDE <= CTL_BASE + CTL_WINDOW, "channel blocks must fit the CTL window");
    static_assert(CTL_BASE + CTL_WINDOW <= OUT_RING0, "CTL window must end below the rings");

    inline constexpr u32 channelBlockAddr(u32 c) { return CHANNEL_BLOCK0 + c * CHANNEL_BLOCK_STRIDE; }
    inline constexpr u32 outRingAddr(u32 c) { return OUT_RING0 + c * RING_BYTES; }
    inline constexpr u32 inRingAddr(u32 c) { return IN_RING0 + c * RING_BYTES; }
    /** Destination car address in the E1 of channel c for car index i: what T6EP's mDestBlockAddr + CAR_SIZE*carindex must equal. */
    inline constexpr u32 outCarAddr(u32 c, u32 carindex) { return outRingAddr(c) + carindex * CAR_BYTES; }

    //// PART 2: assignment ////////////////////////////////////////////////////////////////////////////////////////////////

    enum Slot : u8 { SLOT_CARDINAL = 0, SLOT_DIAG_LO = 1, SLOT_DIAG_HI = 2 };
    static constexpr u32 MAX_LINKS = 4u;            //< live links per seam (QuietBox: 4)

    /** Board size in compact T6 coordinates and the edge-hub counts per cell size.  A cell is cw x ch T6s with ONE hub in it
        (the cell types in nu1012.dtoml: C2X2 / C2X1 / C1X2 / C1X1).  East/west seams have one edge hub per cell ROW,
        north/south seams one per cell COLUMN.  (2,2) -> E/W 5, N/S 7.  Assumes the board dimensions divide by the cell size. */
    static constexpr u32 BOARD_CT6_W = 14u;
    static constexpr u32 BOARD_CT6_H = 10u;
    inline constexpr u32 edgeHubsEW(u32 cellH) { return cellH ? BOARD_CT6_H / cellH : 0u; }
    inline constexpr u32 edgeHubsNS(u32 cellW) { return cellW ? BOARD_CT6_W / cellW : 0u; }
    /** Edge index of the hub whose cell contains CT6 coordinate 'along' (the coordinate ALONG the edge: y for E/W, x for N/S). */
    inline constexpr u32 edgeIndex(u32 along, u32 cellDimAlong) { return cellDimAlong ? along / cellDimAlong : 0u; }

    /** Directed storages crossing ONE seam in ONE direction: E cardinal + 2(E-1) diagonal. */
    inline constexpr u32 channelsTwoCards(u32 E) { return E == 0u ? 0u : 3u * E - 2u; }
    /** Upper bound when the cards form a ring and corner diagonals can cross a second seam: 3E. */
    inline constexpr u32 channelsFullRing(u32 E) { return 3u * E; }

    /** Does storage (e, slot) exist on a seam with E edge hubs?  (No diagonal toward a non-existent neighbor.) */
    inline constexpr bool slotExists(u32 E, u32 e, Slot s) {
      return e < E && (s == SLOT_CARDINAL || (s == SLOT_DIAG_LO && e > 0u) || (s == SLOT_DIAG_HI && e + 1u < E));
    }

    /** Canonical ordinal of storage (e, slot) among the channelsTwoCards(E) storages: hub by hub, CARDINAL, LO, HI. */
    inline u32 ordinalOf(u32 e, Slot s) {
      u32 before = (e == 0u) ? 0u : 3u * e - 1u;          // hub 0 has 2 slots (CARDINAL, HI), the others 3
      if (s == SLOT_CARDINAL) return before;
      if (s == SLOT_DIAG_LO) return before + 1u;
      return before + 1u + (e > 0u ? 1u : 0u);            // HI
    }

    struct Assign { u8 link; u8 channel; };               //< link index into the seam's live list (0..L-1), channel number on that link's E1

    /** BALANCED DEALING: deal the canonical ordinals round-robin over the L live links.  Link counts differ by at most one.
        Returns false if (e,slot) does not exist or L is 0 or > MAX_LINKS. */
    inline bool assign(u32 E, u32 L, u32 e, Slot s, Assign & out) {
      if (L == 0u || L > MAX_LINKS || !slotExists(E, e, s)) return false;
      u32 ord = ordinalOf(e, s);
      out.link = (u8) (ord % L);
      out.channel = (u8) (ord / L);
      return true;
    }

    /** Inverse: which storage does (link, channel) carry?  False if that channel is unused. */
    inline bool invert(u32 E, u32 L, u32 link, u32 channel, u32 & e, Slot & s) {
      if (L == 0u || L > MAX_LINKS || link >= L) return false;
      u32 ord = channel * L + link;
      if (ord >= channelsTwoCards(E)) return false;
      for (u32 h = 0u; h < E; ++h) {
        for (u32 k = 0u; k < 3u; ++k) {
          Slot sl = (Slot) k;
          if (slotExists(E, h, sl) && ordinalOf(h, sl) == ord) { e = h; s = sl; return true; }
        }
      }
      return false;
    }

    /** Most channels any one link's E1 must serve for 'n' directed storages dealt over L links: ceil(n/L). */
    inline constexpr u32 maxChannelsPerLink(u32 n, u32 L) { return L ? (n + L - 1u) / L : 0u; }
    /** Does that fit in an E1 built with NCH_MAX channels? */
    inline constexpr bool fits(u32 n, u32 L) { return L != 0u && maxChannelsPerLink(n, L) <= NCH_MAX; }

    /** Live-link ETH tiles.  The seam's list is ETH NoC0 x coordinates in row y == 1 (the same x on both cards of a pair), in the
        order link index 0..L-1.  DEFAULTS FOR THIS QUIETBOX ONLY (cabling facts from the firmware peer info, ERISC_NOTES section 6):
        E/W seam cards 0-1 and 2-3: X = 6, 7, 10, 11; N/S seam cards 0-2 and 1-3: X = 3, 4, 12, 13.  Only X=6 (x seam) and X=3
        (y seam) have been exercised on hardware so far; the host must confirm each link is up before listing it. */
    struct SeamLinks { u8 n; u8 ethX[MAX_LINKS]; };
    static constexpr SeamLinks DEFAULT_EW_LINKS = { 4u, { 6u, 7u, 10u, 11u } };
    static constexpr SeamLinks DEFAULT_NS_LINKS = { 4u, { 3u, 4u, 12u, 13u } };
    static constexpr u32 ETH_NOC0_Y = 1u;

    //// PART 3: E1 control words, flags, modes (e1fwd13..16; the E1 images carry their own copies of these) ///////////////

    // CTL block word offsets from CTL_BASE
    static constexpr u32 CTL_GO = 0x00u, CTL_STOP = 0x04u, CTL_READY = 0x08u, CTL_DONE = 0x0Cu, CTL_MODE = 0x10u, CTL_FLAGS = 0x18u,
      CTL_NCH = 0x1Cu, CTL_VERIFY_EVERY = 0x20u, CTL_RING_CARS = 0x24u, CTL_SWEEPS = 0x30u, CTL_NODE_ID = 0x44u, CTL_IMAGE_ID = 0x54u,
      CTL_NIU_BUF = 0x58u;
    static constexpr u32 E1_READY_WORD = 0xE1E1E1E1u;
    // channel block word indices (u32 index within CHANNEL_BLOCK(c)): config 0..7, stats 8..23
    enum ChannelWord : u8 { CW_OUT = 0, CW_IN = 1, CW_PEERIN = 2, CW_T6NODE = 3, CW_T6BASE = 4, CW_LEGS = 5,
                            CW_S_FWD = 8, CW_S_INSEEN = 9, CW_S_REFL = 10, CW_S_TXFAIL = 11, CW_S_NOCW = 20, CW_S_NOCFAIL = 21, CW_S_PARTIAL = 22 };
    enum E1Mode : u8 { E1_MODE_ROUTER = 5, E1_MODE_REFLECT = 6 };
    enum E1Flag : u8 { E1F_FENCE = 1, E1F_CSR = 2, E1F_REGEN = 4, E1F_VCSTATIC = 8, E1F_TXQFIX = 16, E1F_NOC1 = 32 };
    enum E1Leg : u8 { LEG_OUT = 1, LEG_IN_NOC = 2, LEG_IN_COUNT = 4 };
    static constexpr u32 E1_IMAGE_ID_MIN = 0xE1F00013u, E1_IMAGE_ID_MAX = 0xE1F00016u;   //< e1fwd13..16 (e1fwd16 is the current image)

  } // namespace EthSeam
} // namespace MFM
