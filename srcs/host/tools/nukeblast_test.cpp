// nukeblast_test.cpp -- native test of the nuke geometry (shared/include/
// NukeBlast.h) over the REAL hub layout (DemoGlobal.h): the self regions of the
// 140 hubs tile the global grid exactly once, and a blast applied hub by hub
// the way GridManager::applyNuke does (whole T6Grid, caches included, decided
// from each site's global coordinate) erases exactly the disc, with every
// cache copy agreeing with its owner.  Build/run:
//   see the nukeblast_test line of run_native_tests.sh for the g++ command
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>

#include "NukeBlast.h"

using namespace MFM;

namespace {
int g_bad = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++g_bad;
  }
}

constexpr s32 GW = DG::BLACKHOLE_ACTIVE_HUB_WIDTH * DG::BLACKHOLE_CHIP_ARRAY_WIDTH * DG::T6GRID_BASE_WIDTH;
constexpr s32 GH = DG::BLACKHOLE_ACTIVE_HUB_HEIGHT * DG::BLACKHOLE_CHIP_ARRAY_HEIGHT * DG::T6GRID_BASE_HEIGHT;
constexpr s32 OW = DG::T6GRID_OVERLAP_WIDTH;
constexpr s32 OH = DG::T6GRID_OVERLAP_HEIGHT;
constexpr s32 FW = DG::T6GRID_WIDTH;
constexpr s32 FH = DG::T6GRID_HEIGHT;
constexpr s32 BW = DG::T6GRID_BASE_WIDTH;
constexpr s32 BH = DG::T6GRID_BASE_HEIGHT;

struct Hub {
  u32 chip, hx, hy;
  s32 ox, oy;                   // global origin of the first SELF site
};

// One hub applying the blast to an all-occupied grid, as applyNuke does.
// erased[x + FW*y] marks the full-grid sites it erased.
u32 hubApply(const Hub& h, const NukeBlast& nb, std::vector<char>& erased,
             u32& cacheErased) {
  const s32 gx0 = h.ox - OW, gy0 = h.oy - OH;
  erased.assign((size_t) FW * FH, 0);
  if (!nb.mayTouchBox(gx0, gy0, gx0 + FW, gy0 + FH)) return 0;
  u32 self = 0;
  for (s32 x = 0; x < FW; ++x)
    for (s32 y = 0; y < FH; ++y) {
      if (!nb.hits(gx0 + x, gy0 + y)) continue;
      erased[(size_t) x + (size_t) FW * y] = 1;
      const bool inSelf = x >= OW && x < FW - OW && y >= OH && y < FH - OH;
      if (inSelf) ++self;
      else ++cacheErased;
    }
  return self;
}
}  // namespace

int main() {
  check(GW == 1918 && GH == 1080, "global grid is 1918 x 1080 sites");

  std::vector<Hub> hubs;
  for (u32 chip = 0; chip < 4; ++chip)
    for (u32 hx = 0; hx < DG::BLACKHOLE_ACTIVE_HUB_WIDTH; ++hx)
      for (u32 hy = 0; hy < DG::BLACKHOLE_ACTIVE_HUB_HEIGHT; ++hy) {
        Hub h{chip, hx, hy, 0, 0};
        hubSelfOriginGlobal(chip, hx, hy, h.ox, h.oy);
        hubs.push_back(h);
      }
  check(hubs.size() == 140, "140 hubs");

  // tiling: every global site is a SELF site of exactly one hub
  std::vector<unsigned char> owner((size_t) GW * GH, 0);
  for (const Hub& h : hubs)
    for (s32 x = 0; x < BW; ++x)
      for (s32 y = 0; y < BH; ++y) {
        const s32 gx = h.ox + x, gy = h.oy + y;
        if (gx < 0 || gy < 0 || gx >= GW || gy >= GH) {
          check(false, "a hub's self site is outside the global grid");
          continue;
        }
        ++owner[(size_t) gx + (size_t) GW * gy];
      }
  size_t notOnce = 0;
  for (unsigned char c : owner) notOnce += (c != 1);
  check(notOnce == 0, "self regions tile the global grid exactly once");

  // blasts: the union of the hubs' SELF erasures is exactly the disc, and the
  // cache copies agree with their owners
  std::mt19937 rng(12345);
  std::vector<char> erased;
  struct Case { s32 x, y, r; };
  std::vector<Case> cases = {
    {960, 540, 40}, {0, 0, 30}, {GW - 1, GH - 1, 54}, {0, GH - 1, 504},
    {GW / 2, GH / 2, 504}, {137, 108, 25}, {136, 107, 5}, {5, 5, 1},
  };
  for (int i = 0; i < 25; ++i)
    cases.push_back({(s32) (rng() % GW), (s32) (rng() % GH), (s32) (rng() % 504) + 1});

  for (const Case& cs : cases) {
    NukeBlast nb{cs.x, cs.y, cs.r * cs.r};
    std::vector<char> globalErasedSelf((size_t) GW * GH, 0);
    std::vector<std::vector<char>> per(hubs.size());
    u64 totalSelf = 0;
    for (size_t i = 0; i < hubs.size(); ++i) {
      u32 cache = 0;
      totalSelf += hubApply(hubs[i], nb, per[i], cache);
      const Hub& h = hubs[i];
      for (s32 x = OW; x < FW - OW; ++x)
        for (s32 y = OH; y < FH - OH; ++y)
          if (per[i][(size_t) x + (size_t) FW * y])
            globalErasedSelf[(size_t) (h.ox + x - OW) + (size_t) GW * (h.oy + y - OH)] = 1;
    }
    u64 want = 0;
    bool diff = false;
    for (s32 gx = 0; gx < GW; ++gx)
      for (s32 gy = 0; gy < GH; ++gy) {
        const bool in = nb.hits(gx, gy);
        want += in;
        if (in != (bool) globalErasedSelf[(size_t) gx + (size_t) GW * gy]) diff = true;
      }
    check(!diff, "erased self sites == the exact disc");
    check(totalSelf == want, "self count == disc area inside the grid");
    // cache agreement: a cache site that maps onto a real global site is
    // erased iff that site's owner erased it (same predicate on the same coord)
    bool cacheBad = false;
    for (size_t i = 0; i < hubs.size() && !cacheBad; ++i) {
      const Hub& h = hubs[i];
      for (s32 x = 0; x < FW && !cacheBad; ++x)
        for (s32 y = 0; y < FH; ++y) {
          const bool inSelf = x >= OW && x < FW - OW && y >= OH && y < FH - OH;
          if (inSelf) continue;
          const s32 gx = h.ox + x - OW, gy = h.oy + y - OH;
          if (gx < 0 || gy < 0 || gx >= GW || gy >= GH) continue;
          if ((bool) per[i][(size_t) x + (size_t) FW * y] !=
              (bool) globalErasedSelf[(size_t) gx + (size_t) GW * gy]) {
            cacheBad = true;
            break;
          }
        }
    }
    check(!cacheBad, "every cache copy agrees with its owner");
  }

  // mayTouchBox never rejects a box that contains a hit
  for (int i = 0; i < 3000; ++i) {
    NukeBlast nb{(s32) (rng() % 400) - 100, (s32) (rng() % 400) - 100, (s32) (rng() % 5000) + 1};
    s32 x0 = (s32) (rng() % 300) - 100, y0 = (s32) (rng() % 300) - 100;
    s32 x1 = x0 + 1 + (s32) (rng() % 120), y1 = y0 + 1 + (s32) (rng() % 120);
    bool any = false;
    for (s32 x = x0; x < x1 && !any; ++x)
      for (s32 y = y0; y < y1; ++y)
        if (nb.hits(x, y)) {
          any = true;
          break;
        }
    if (any && !nb.mayTouchBox(x0, y0, x1, y1)) {
      check(false, "mayTouchBox rejected a box containing a hit");
      break;
    }
  }
  NukeBlast none{5, 5, 0};
  check(none.isNone() && !none.hits(5, 5), "r2 == 0 is no blast");

  printf(g_bad ? "FAIL\n" : "nukeblast_test ok\n");
  return g_bad ? 1 : 0;
}
