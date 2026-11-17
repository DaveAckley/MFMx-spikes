// ethseam_test.cpp -- host-side checks of shared/include/EthSeam.h.
//
// Build/run: srcs/host/tools/run_native_tests.sh ethseam
//
// Checks:
//  - layout constants equal what the E1 images and ethfwd14..20 use;
//  - for every cell geometry (2x2, 2x1, 1x2, 1x1), seam kind and live link
//    count 1..4: the ordinals are a bijection, assign/invert round-trip,
//    and links are dealt in balance (counts differ by at most one);
//  - Dir4 seam wiring: sender dest, host receive plan and far hub agree;
//  - the HostBlock link-mask encoding.
// It also prints the channels-per-link table with the fits-in-NCH_MAX
// verdicts used in ERISC_SEAM_DESIGN.md section 13.
#include "EthSeam.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace MFM;
using namespace MFM::EthSeam;

// ---- layout must equal the tools' copies (ethfwd14..20 / e1fwd13..16) ----

static_assert(NCH_MAX == 8, "ring geometry");
static_assert(CAR_BYTES == 0x1040, "ring geometry");
static_assert(RING_CARS == 4, "ring geometry");
static_assert(RING_BYTES == 0x4100, "ring geometry");
static_assert(CHUNK_BYTES == 1456, "ring geometry");

static_assert(E1_CODE == 0x4000, "CTL layout");
static_assert(CTL_BASE == 0x8000, "CTL layout");
static_assert(CTL_WINDOW == 0x600, "CTL layout");
static_assert(CHANNEL_BLOCK0 == 0x8100, "CTL layout");
static_assert(CHANNEL_BLOCK_STRIDE == 0x80, "CTL layout");

static_assert(OUT_RING0 == 0x10000, "ring placement");
static_assert(IN_RING0 == 0x30800, "ring placement");
static_assert(channelBlockAddr(3) == 0x8100 + 3 * 0x80, "ring placement");
static_assert(outCarAddr(2, 1) == 0x10000 + 2 * 0x4100 + 0x1040,
              "car address");

static_assert(CTL_GO == 0, "CTL words");
static_assert(CTL_STOP == 4, "CTL words");
static_assert(CTL_READY == 8, "CTL words");
static_assert(CTL_DONE == 0xC, "CTL words");
static_assert(CTL_MODE == 0x10, "CTL words");
static_assert(CTL_FLAGS == 0x18, "CTL words");
static_assert(CTL_NCH == 0x1C, "CTL words");
static_assert(CTL_RING_CARS == 0x24, "CTL words");
static_assert(CTL_NODE_ID == 0x44, "CTL words");
static_assert(CTL_IMAGE_ID == 0x54, "CTL words");
static_assert(CTL_NIU_BUF == 0x58, "CTL words");

static_assert(E1F_CSR == 2, "flags and channel words");
static_assert(E1F_TXQFIX == 16, "flags and channel words");
static_assert(E1F_NOC1 == 32, "flags and channel words");
static_assert(E1_MODE_ROUTER == 5, "flags and channel words");
static_assert(LEG_IN_NOC == 2, "flags and channel words");
static_assert(CW_T6BASE == 4, "flags and channel words");
static_assert(CW_LEGS == 5, "flags and channel words");
static_assert(CW_S_FWD == 8, "flags and channel words");

// ---- the design doc's channel counts ----

static_assert(channelsTwoCards(5) == 13, "2x2 counts");
static_assert(channelsFullRing(5) == 15, "2x2 counts");
static_assert(channelsTwoCards(7) == 19, "2x2 counts");
static_assert(channelsFullRing(7) == 21, "2x2 counts");

static_assert(edgeHubsEW(2) == 5, "edge hubs");
static_assert(edgeHubsNS(2) == 7, "edge hubs");
static_assert(edgeHubsEW(1) == 10, "edge hubs");
static_assert(edgeHubsNS(1) == 14, "edge hubs");

static int fails = 0;

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      printf("FAIL line %d: %s\n", __LINE__, #c);         \
      ++fails;                                            \
    }                                                     \
  } while (0)

// Balanced dealing on a seam with E edge hubs, for 1..4 live links.
static void check_seam(u32 E) {
  u32 N = channelsTwoCards(E);
  for (u32 L = 1; L <= MAX_LINKS; ++L) {
    int seen[64] = { 0 };
    u32 perLink[MAX_LINKS] = { 0 };
    u32 maxch[MAX_LINKS] = { 0 };
    u32 keys = 0;

    for (u32 e = 0; e < E; ++e) {
      for (u32 k = 0; k < 3; ++k) {
        Slot s = (Slot) k;
        Assign a;
        bool exists = slotExists(E, e, s);
        bool dealt = assign(E, L, e, s, a);
        CHECK(exists == dealt);
        if (!dealt) continue;
        ++keys;
        CHECK(a.link < L);

        u32 ord = ordinalOf(e, s);
        CHECK(ord < N);
        CHECK(ord < 64);
        if (ord < 64) ++seen[ord];

        ++perLink[a.link];
        if (a.channel + 1u > maxch[a.link]) maxch[a.link] = a.channel + 1u;

        u32 e2 = 99;
        Slot s2 = SLOT_DIR4;
        CHECK(invert(E, L, a.link, a.channel, e2, s2));
        CHECK(e2 == e);
        CHECK(s2 == s);
      }
    }
    CHECK(keys == N);
    for (u32 i = 0; i < N; ++i) CHECK(seen[i] == 1);

    u32 mn = 1000;
    u32 mx = 0;
    for (u32 j = 0; j < L; ++j) {
      if (perLink[j] < mn) mn = perLink[j];
      if (perLink[j] > mx) mx = perLink[j];
      CHECK(maxch[j] == perLink[j]);
    }
    CHECK(mx - mn <= 1);                        // balanced
    CHECK(mx == maxChannelsPerLink(N, L));

    Assign dummy;
    u32 ee;
    Slot ss;
    CHECK(!assign(E, L, E, SLOT_DIR4, dummy));  // no such hub
    CHECK(!invert(E, L, 0, N, ee, ss));         // unused channel
  }

  // L == 0 or > MAX_LINKS is rejected
  Assign z;
  CHECK(!assign(E, 0, 0, SLOT_DIR4, z));
  CHECK(!assign(E, MAX_LINKS + 1, 0, SLOT_DIR4, z));
}

// Dir4 seam wiring (PART 4): the sending hub's dest, the host's receive
// plan and the far hub's own dest must all agree.
static void check_dir4() {
  const CellShape c = CELL_C2X2;
  for (u32 L = 1; L <= MAX_LINKS; ++L) {
    SeamTable a;
    SeamTable b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    for (u32 i4 = 0; i4 < 4; ++i4) {
      Dir4 d4 = (Dir4) i4;
      Dir4 back = oppositeDir4(d4);
      a.n[d4] = (u8) L;
      b.n[back] = (u8) L;
      for (u32 i = 0; i < L; ++i) {
        a.ethX[d4][i] = (u8) (3 + i);
        b.ethX[back][i] = (u8) (3 + i);
      }
    }

    for (u32 i4 = 0; i4 < 4; ++i4) {
      Dir4 d4 = (Dir4) i4;
      Dir4 back = oppositeDir4(d4);
      u32 E = edgeHubCount(d4, c);
      u32 hubs = 0;
      CHECK(weAreInDir4(d4) != weAreInDir4(back));

      for (u32 y = 0; y < BOARD_CT6_H; ++y) {
        for (u32 x = 0; x < BOARD_CT6_W; ++x) {
          // only hubs count: the hub's offset within its cell
          bool hub = (x % c.cw == c.hox) && (y % c.ch == c.hoy);
          DestDir4 da = { 0, 0 };
          bool got = destDir4(a, d4, x, y, c, da);
          CHECK(got == (hub && hubOnEdge(d4, x, y, c)));
          if (!got) continue;
          ++hubs;

          // the host plan for that (link, channel)
          u32 link = 99;
          for (u32 i = 0; i < L; ++i) {
            if (a.ethX[d4][i] == da.ethx) link = i;
          }
          CHECK(link < L);
          RecvDir4 r = { 0, 0, 0 };
          CHECK(recvDir4(a, d4, link, da.channel, c, r));

          // the receiving hub is on the facing edge, at the same index
          CHECK(hubOnEdge(back, r.recvCT6x, r.recvCT6y, c));
          CHECK(hubEdgeIndex(back, r.recvCT6x, r.recvCT6y, c) ==
                hubEdgeIndex(d4, x, y, c));
          CHECK(r.recvDir8 == dir4ToDir8(back));

          // and its own dest toward us uses the same ETH tile + channel
          DestDir4 db = { 0, 0 };
          CHECK(destDir4(b, back, r.recvCT6x, r.recvCT6y, c, db));
          CHECK(db.ethx == da.ethx);
          CHECK(db.channel == da.channel);
        }
      }
      CHECK(hubs == E);
    }
  }

  // hubs not on an edge, and seams with no links, give nothing
  SeamTable none;
  memset(&none, 0, sizeof(none));
  DestDir4 d = { 0, 0 };
  CHECK(!destDir4(none, D4_E, 13, 1, c, d));

  SeamTable one = none;
  one.n[D4_E] = 1;
  one.ethX[D4_E][0] = 6;
  CHECK(!destDir4(one, D4_E, 11, 1, c, d));     // not on the E edge
  CHECK(destDir4(one, D4_E, 13, 9, c, d));
  CHECK(d.ethx == 6);
}

// Mask encoding: HostBlock word <-> SeamTable, cabling lists.
static void check_mask() {
  u8 ew[2] = { 10, 6 };
  u32 nib = 0;
  CHECK(linkNibble(D4_E, ew, 2, nib));
  CHECK(nib == 0x5);

  u8 bad[1] = { 3 };
  CHECK(!linkNibble(D4_E, bad, 1, nib));        // N/S tile, E/W seam
  u8 dup[2] = { 6, 6 };
  CHECK(!linkNibble(D4_E, dup, 2, nib));
  CHECK(!linkNibble(D4_E, ew, 0, nib));

  u16 mask = 0;
  mask = packSeamMask(D4_E, 0x5, mask);
  mask = packSeamMask(D4_N, 0xf, mask);
  mask = packSeamMask(D4_E, 0x5, mask);         // idempotent
  CHECK(mask == (u16) ((0x5u << 12) | 0xfu));

  SeamTable t;
  decodeSeamMask(mask, t);
  CHECK(t.n[D4_E] == 2);
  CHECK(t.ethX[D4_E][0] == 6);
  CHECK(t.ethX[D4_E][1] == 10);
  CHECK(t.n[D4_N] == 4);
  CHECK(t.ethX[D4_N][3] == 13);
  CHECK(t.n[D4_W] == 0);
  CHECK(t.n[D4_S] == 0);

  // every possible mask decodes to link counts equal to the popcount
  for (u32 m = 0; m < 0x10000; ++m) {
    decodeSeamMask((u16) m, t);
    for (u32 d = 0; d < 4; ++d) {
      u32 pc = __builtin_popcount((m >> (4 * d)) & 0xf);
      CHECK(t.n[d] == pc);
    }
  }
}

// ---- PART 5: diagonal crossings, against an oracle ----
//
// The oracle is InterHub.cpp's own two-hop arithmetic (manageInterHubNC for
// hop 1, manageCornerHubNC for hop 2), run over a GLOBAL hub grid of two
// chips side by side, with every hop that crosses the chip boundary
// recorded.  diagRoute / diagForwarderSlot must name exactly those.

struct OracleCross {
  u32 srcEdge;                  //< edge index of the source hub
  Dir8 corner;                  //< its corner endpoint
  Dir4 block;                   //< the CHUB block of hop 1's target hub
  DiagHop hop;
  u32 crossEdge;                //< edge index of the hub that writes to E1
  u32 recvEdge;                 //< edge index of the receiving hub
  RecvBlock recvBlock;
  u32 recvIndex;
};

static void vec8(Dir8 d, int & dx, int & dy) {
  static const int V[8][2] = {
    { 0, -1 }, { -1, -1 }, { -1, 0 }, { -1, 1 },
    { 0, 1 }, { 1, 1 }, { 1, 0 }, { 1, -1 }
  };
  dx = V[d][0];
  dy = V[d][1];
}

static void vec4(Dir4 d, int & dx, int & dy) {
  static const int V[4][2] = { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 } };
  dx = V[d][0];
  dy = V[d][1];
}

static void check_diag_seam(Dir4 seam) {
  const bool eastWest = (seam == D4_E || seam == D4_W);
  // two chips side by side along the seam axis, 7 x 5 hubs each
  const int GW = eastWest ? 14 : 7;
  const int GH = eastWest ? 5 : 10;
  const int HW = 7;
  const int HH = 5;
  // chip 0 is the west / north half, chip 1 the east / south half
  auto chipOf = [&](int x, int y) {
    return eastWest ? (x >= HW ? 1 : 0) : (y >= HH ? 1 : 0);
  };
  auto inGrid = [&](int x, int y) {
    return x >= 0 && y >= 0 && x < GW && y < GH;
  };
  auto edgeIdx = [&](int x, int y) { return (u32) (eastWest ? y : x); };
  // the chip whose seam direction is 'seam': it sends toward the other
  const int A = (seam == D4_E || seam == D4_S) ? 0 : 1;
  const u32 E = eastWest ? 5 : 7;

  std::vector<OracleCross> found;
  for (int y = 0; y < GH; ++y) {
    for (int x = 0; x < GW; ++x) {
      if (chipOf(x, y) != A) continue;
      for (u32 c = 1; c < 8; c += 2) {
        Dir8 corner = (Dir8) c;
        int dx, dy;
        vec8(corner, dx, dy);
        int fx = x + dx;                  // the final hub F
        int fy = y + dy;
        // hop 1, as InterHub.cpp: P is the hub CCW of the corner
        Dir8 first = dir8CornerToCCWDir8Edge(corner);
        vec8(first, dx, dy);
        int px = x + dx;                  // the first-hop hub P
        int py = y + dy;
        if (!inGrid(fx, fy) || !inGrid(px, py)) continue;
        Dir4 block = dir8ToDir4CW(first);
        // hop 2, as manageCornerHubNC: P's forwarder for 'block'
        Dir4 toward = nextCWDir4(block);
        vec4(toward, dx, dy);
        int qx = px + dx;
        int qy = py + dy;
        CHECK(qx == fx && qy == fy);      // hop 2 reaches F by construction
        u32 recvIdx =
          (u32) dir8ToCWDir8(dir4ToDir8(oppositeDir4(block)));

        OracleCross o;
        o.srcEdge = edgeIdx(x, y);
        o.corner = corner;
        o.block = block;
        if (chipOf(px, py) != A) {
          // hop 1 crosses; hop 2 must stay on the far chip
          CHECK(chipOf(fx, fy) == chipOf(px, py));
          o.hop = DIAG_FIRST_HOP;
          o.crossEdge = edgeIdx(x, y);
          o.recvEdge = edgeIdx(px, py);
          o.recvBlock = RECV_CHUB;
          o.recvIndex = (u32) block;
          found.push_back(o);
        } else if (chipOf(fx, fy) != A) {
          // hop 1 stays; the forwarder at P crosses
          o.hop = DIAG_SECOND_HOP;
          o.crossEdge = edgeIdx(px, py);
          o.recvEdge = edgeIdx(fx, fy);
          o.recvBlock = RECV_IHUB;
          o.recvIndex = recvIdx;
          found.push_back(o);
        }
      }
    }
  }

  // a seam with E edge hubs has exactly E-1 diagonals each way
  CHECK(found.size() == 2 * (E - 1));

  for (u32 e = 0; e < E; ++e) {
    for (u32 k = 1; k <= 2; ++k) {
      Slot s = (Slot) k;
      DiagRoute r;
      bool ok = diagRoute(seam, E, e, s, r);
      CHECK(ok == slotExists(E, e, s));
      if (!ok) continue;

      const OracleCross * o = nullptr;
      u32 matches = 0;
      for (const OracleCross & x : found) {
        if (x.srcEdge == e && (u32) x.corner == r.srcDir8) {
          o = &x;
          ++matches;
        }
      }
      CHECK(matches == 1);
      if (!o) continue;
      CHECK(r.hop == o->hop);
      CHECK(r.crossEdge == o->crossEdge);
      CHECK(r.recvEdge == o->recvEdge);
      CHECK(r.recvBlock == o->recvBlock);
      CHECK(r.recvIndex == o->recvIndex);

      // the forwarder's own view finds the same channel name
      if (o->hop == DIAG_SECOND_HOP) {
        u32 eSrc = 99;
        Slot fs = SLOT_DIR4;
        CHECK(diagForwarderSlot(seam, E, o->crossEdge, o->block, eSrc, fs));
        CHECK(eSrc == e);
        CHECK(fs == s);
      }
    }
  }

  // of a hub's four CHUB blocks, exactly one has a forwarder that ships
  // across this seam; the others say they do not
  u32 shipping = 0;
  for (u32 b = 0; b < 4; ++b) {
    u32 eSrc = 99;
    Slot fs = SLOT_DIR4;
    if (diagForwarderSlot(seam, E, 2, (Dir4) b, eSrc, fs)) ++shipping;
  }
  CHECK(shipping == 1);
  // and a DIR4 slot is not a diagonal
  DiagRoute dr;
  CHECK(!diagRoute(seam, E, 1, SLOT_DIR4, dr));
}

// The T6-side lookups (diagSend for the source hub, diagForwardDest for the
// forwarder) against the planner's own dealing: for every live-link count,
// every hub and every corner of a chip, exactly the 2(E-1) diagonal crossings
// answer, and the forwarder's channel is the one the source hub names.
static void check_diag_t6(Dir4 seam) {
  const CellShape & c = CELL_C2X2;
  const SeamLinks & cab = cablingFor(seam);
  const u32 E = edgeHubCount(seam, c);
  for (u32 L = 1; L <= cab.n; ++L) {
    SeamTable t;
    memset(&t, 0, sizeof(t));
    t.n[seam] = (u8) L;
    for (u32 i = 0; i < L; ++i) t.ethX[seam][i] = cab.ethX[i];

    u32 answers = 0;
    for (u32 y = 0; y < BOARD_CT6_H; ++y) {
      for (u32 x = 0; x < BOARD_CT6_W; ++x) {
        for (u32 d = 1; d < 8; d += 2) {
          DiagSend ds;
          if (!diagSend(t, (Dir8) d, x, y, c, ds)) continue;
          ++answers;
          CHECK(hubOnEdge(seam, x, y, c));
          CHECK(ds.seam == seam);
          // the same channel the planner deals to (edge index, slot)
          u32 e = hubEdgeIndex(seam, x, y, c);
          Dir4 a = dir8ToDir4CW((Dir8) ((d + 7u) & 7u));
          Dir4 along = (a == seam) ? dir8ToDir4CW((Dir8) ((d + 1u) & 7u)) : a;
          Slot s = (alongEdge(seam, SLOT_DIAG_LO) == along)
            ? SLOT_DIAG_LO : SLOT_DIAG_HI;
          Assign as = { 0, 0 };
          CHECK(assign(E, L, e, s, as));
          CHECK(ds.ethx == t.ethX[seam][as.link]);
          CHECK(ds.channel == as.channel);
          DiagRoute r = { DIAG_FIRST_HOP, 0, 0, 0, RECV_IHUB, 0 };
          CHECK(diagRoute(seam, E, e, s, r));
          CHECK(ds.hop == r.hop);
          CHECK(r.srcDir8 == d);
          if (r.hop == DIAG_SECOND_HOP) {
            // the forwarder is the hub at crossEdge, block = direction
            // from this hub to it
            Dir8 first = dir8CornerToCCWDir8Edge((Dir8) d);
            Dir4 block = dir8ToDir4CW(first);
            u32 px = 0;
            u32 py = 0;
            CHECK(edgeHubCT6(seam, r.crossEdge, c, px, py));
            DestDir4 fd = { 0, 0 };
            CHECK(diagForwardDest(t, block, px, py, c, fd));
            CHECK(fd.ethx == ds.ethx);
            CHECK(fd.channel == ds.channel);
          }
        }
      }
    }
    CHECK(answers == 2 * (E - 1));

    // a forwarder answers only for its one block on edge hubs, and exactly
    // as many times as there are second-hop crossings
    u32 fwd = 0;
    u32 second = 0;
    for (u32 e = 0; e < E; ++e) {
      u32 x = 0;
      u32 y = 0;
      CHECK(edgeHubCT6(seam, e, c, x, y));
      for (u32 b = 0; b < 4; ++b) {
        DestDir4 fd;
        if (diagForwardDest(t, (Dir4) b, x, y, c, fd)) ++fwd;
      }
      for (Slot s : { SLOT_DIAG_LO, SLOT_DIAG_HI }) {
        DiagRoute r = { DIAG_FIRST_HOP, 0, 0, 0, RECV_IHUB, 0 };
        if (diagRoute(seam, E, e, s, r) && r.hop == DIAG_SECOND_HOP) {
          ++second;
        }
      }
    }
    CHECK(fwd == second);
    // not live: nothing
    SeamTable none;
    memset(&none, 0, sizeof(none));
    DiagSend ds;
    DestDir4 fd;
    CHECK(!diagSend(none, D8_SE, 13, 1, c, ds));
    CHECK(!diagForwardDest(none, D4_E, 13, 1, c, fd));
  }
}

static void check_diag() {
  check_diag_seam(D4_E);
  check_diag_seam(D4_W);
  check_diag_seam(D4_S);
  check_diag_seam(D4_N);
  check_diag_t6(D4_E);
  check_diag_t6(D4_W);
  check_diag_t6(D4_S);
  check_diag_t6(D4_N);

  // spot checks of the two directions on an east seam, written out
  DiagRoute r;
  CHECK(diagRoute(D4_E, 5, 2, SLOT_DIAG_HI, r));       // SE: hop 1 crosses
  CHECK(r.hop == DIAG_FIRST_HOP);
  CHECK(r.srcDir8 == D8_SE);
  CHECK(r.crossEdge == 2);
  CHECK(r.recvEdge == 2);
  CHECK(r.recvBlock == RECV_CHUB);
  CHECK(r.recvIndex == D4_E);
  CHECK(diagRoute(D4_E, 5, 2, SLOT_DIAG_LO, r));       // NE: hop 2 crosses
  CHECK(r.hop == DIAG_SECOND_HOP);
  CHECK(r.srcDir8 == D8_NE);
  CHECK(r.crossEdge == 1);
  CHECK(r.recvEdge == 1);
  CHECK(r.recvBlock == RECV_IHUB);
  CHECK(r.recvIndex == D8_SW);
}

int main() {
  struct Geo {
    const char * name;
    u32 cw;
    u32 ch;
  };
  const Geo geos[] = {
    { "2x2 (C2X2_EWP_HUB)", 2, 2 },
    { "2x1 (C2X1_EWP_HUB)", 2, 1 },
    { "1x2 (C1X2_EWP_HUB)", 1, 2 },
    { "1x1 (C1X1_ONE)", 1, 1 }
  };

  printf("NCH_MAX (channels per E1 tile in the images built so far) = %u\n",
         NCH_MAX);
  printf("%-20s %-4s %3s %6s %5s |"
         " channels per link for 4 / 3 / 2 live links"
         " (two cards; full ring in brackets) |"
         " fits NCH_MAX with 4 links?\n",
         "cell", "seam", "E", "chans", "ring");

  for (const Geo & g : geos) {
    for (int ns = 0; ns < 2; ++ns) {
      u32 E = ns ? edgeHubsNS(g.cw) : edgeHubsEW(g.ch);
      check_seam(E);
      u32 N = channelsTwoCards(E);
      u32 R = channelsFullRing(E);
      const char * verdict =
        fits(R, 4)
        ? "yes (even full ring)"
        : (fits(N, 4)
           ? "yes (two cards), NO for the full ring"
           : "NO: needs a larger NCH_MAX (and CTL window)");
      printf("%-20s %-4s %3u %6u %5u |"
             " %2u (%2u) / %2u (%2u) / %2u (%2u) | %s\n",
             g.name,
             ns ? "N/S" : "E/W",
             E,
             N,
             R,
             maxChannelsPerLink(N, 4), maxChannelsPerLink(R, 4),
             maxChannelsPerLink(N, 3), maxChannelsPerLink(R, 3),
             maxChannelsPerLink(N, 2), maxChannelsPerLink(R, 2),
             verdict);
    }
  }

  Assign a;
  CHECK(assign(5, 4, 2, SLOT_DIAG_HI, a));
  CHECK(a.link == (ordinalOf(2, SLOT_DIAG_HI) % 4));

  check_dir4();
  check_mask();
  check_diag();
  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all ok");
  return fails != 0;
}
