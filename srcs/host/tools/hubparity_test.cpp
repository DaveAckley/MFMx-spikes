// hubparity_test.cpp -- checks of the global hub / supercell parity in
// shared/include/DemoGlobal.h.
//
// Build/run (from srcs/host/tools):
//   FLAGS="-std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter"
//   g++ $FLAGS -I../include -I../../shared/include hubparity_test.cpp
//     -o /tmp/hubparity_test && /tmp/hubparity_test   (one command)
//
// A supercell is 2x2 hubs and exactly one hub in it leads.  The property
// that matters: no two hubs that touch (Moore neighbors, including across
// a chip seam) may both be leader, for any leader code.
#include "DemoGlobal.h"
#include <cstdio>

using namespace MFM;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static constexpr s32 HW = DG::BLACKHOLE_ACTIVE_HUB_WIDTH;     // 7
static constexpr s32 HH = DG::BLACKHOLE_ACTIVE_HUB_HEIGHT;    // 5

// Is the hub at chip-relative hub coordinates (x, y) of 'chip' leader
// for 'leaderCode'?  'globalParity' false = the old per-chip counting.
static bool isLeader(u32 chip, s32 x, s32 y, u32 leaderCode,
                     bool globalParity) {
  U8C want = U8C::makeSuperCellCoordFromLeaderCode(leaderCode);
  U8C got = globalParity
    ? DG::getHubSuperCellCoord(chip, x, y)
    : U8C((u8) (x & 1), (u8) (y & 1));
  return got == want;
}

// Count adjacent leader-leader pairs over the whole array of chips.
static u32 adjacentLeaderPairs(u32 chips, bool globalParity) {
  // chips == 2: chips 0,1 side by side; chips == 4: the 2x2 array
  u32 bad = 0;
  for (u32 leader = 0; leader < 4; ++leader) {
    for (u32 c = 0; c < chips; ++c) {
      U16C pc = DG::getChipPositionInArray(c);
      for (s32 y = 0; y < HH; ++y) {
        for (s32 x = 0; x < HW; ++x) {
          if (!isLeader(c, x, y, leader, globalParity)) continue;
          // every Moore neighbor, by global hub coordinates
          for (s32 dy = -1; dy <= 1; ++dy) {
            for (s32 dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0) continue;
              s32 gx = (s32) pc.x * HW + x + dx;
              s32 gy = (s32) pc.y * HH + y + dy;
              if (gx < 0 || gy < 0) continue;
              u32 ncx = (u32) (gx / HW);
              u32 ncy = (u32) (gy / HH);
              u32 nc = ncy * DG::BLACKHOLE_CHIP_ARRAY_WIDTH + ncx;
              if (ncx >= DG::BLACKHOLE_CHIP_ARRAY_WIDTH ||
                  ncy >= DG::BLACKHOLE_CHIP_ARRAY_HEIGHT ||
                  nc >= chips) {
                continue;     // no such chip in this array
              }
              s32 nx = gx - (s32) ncx * HW;
              s32 ny = gy - (s32) ncy * HH;
              if (isLeader(nc, nx, ny, leader, globalParity)) ++bad;
            }
          }
        }
      }
    }
  }
  return bad;
}

int main() {
  // chip hub origins: chip n sits at array position (n%2, n/2)
  CHECK(DG::getChipHubOrigin(0) == U8C(0, 0));
  CHECK(DG::getChipHubOrigin(1) == U8C(7, 0));
  CHECK(DG::getChipHubOrigin(2) == U8C(0, 5));
  CHECK(DG::getChipHubOrigin(3) == U8C(7, 5));

  // and they agree with the site origins the rest of the code uses
  for (u32 c = 0; c < 4; ++c) {
    U16C so = DG::getChipOrigin(c);
    U8C ho = DG::getChipHubOrigin(c);
    CHECK(so.x == ho.x * DG::T6GRID_BASE_WIDTH);
    CHECK(so.y == ho.y * DG::T6GRID_BASE_HEIGHT);
  }

  // chip 0 is unchanged from the old per-chip rule
  for (s32 y = 0; y < HH; ++y) {
    for (s32 x = 0; x < HW; ++x) {
      CHECK(DG::getHubSuperCellCoord(0, x, y) == U8C((u8) (x & 1), (u8) (y & 1)));
    }
  }

  // the old rule is wrong across a seam, the global rule is right
  CHECK(adjacentLeaderPairs(1, false) == 0);   // one chip: fine either way
  CHECK(adjacentLeaderPairs(1, true) == 0);
  CHECK(adjacentLeaderPairs(2, false) > 0);    // E/W seam: the old bug
  CHECK(adjacentLeaderPairs(2, true) == 0);
  CHECK(adjacentLeaderPairs(4, false) > 0);    // 2x2 array: old bug too
  CHECK(adjacentLeaderPairs(4, true) == 0);

  // exactly one leader per 2x2 hub block of the global grid
  for (u32 leader = 0; leader < 4; ++leader) {
    for (s32 gy = 0; gy + 1 < 2 * HH; gy += 2) {
      for (s32 gx = 0; gx + 1 < 2 * HW; gx += 2) {
        u32 n = 0;
        for (s32 dy = 0; dy < 2; ++dy) {
          for (s32 dx = 0; dx < 2; ++dx) {
            s32 x = gx + dx;
            s32 y = gy + dy;
            u32 c = (u32) (y / HH) * 2u + (u32) (x / HW);
            if (isLeader(c, x % HW, y % HH, leader, true)) ++n;
          }
        }
        // the 2x2 block starting at an even global coordinate always
        // holds exactly one leader
        CHECK(n == 1);
      }
    }
  }

  // a neighbor named outside the chip uses the same global counting
  CHECK(DG::getHubSuperCellCoord(0, HW, 0) == DG::getHubSuperCellCoord(1, 0, 0));
  CHECK(DG::getHubSuperCellCoord(1, -1, 0) == DG::getHubSuperCellCoord(0, HW - 1, 0));
  CHECK(DG::getHubSuperCellCoord(0, 0, HH) == DG::getHubSuperCellCoord(2, 0, 0));
  CHECK(DG::getHubSuperCellCoord(2, 0, -1) == DG::getHubSuperCellCoord(0, 0, HH - 1));

  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all ok");
  return fails != 0;
}
