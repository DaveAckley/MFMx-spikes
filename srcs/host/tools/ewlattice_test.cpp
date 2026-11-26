// ewlattice_test.cpp -- native test of shared/include/EWLattice.h (the
// perfect EW tiling and the rect-snap hub geometry) over the REAL hub
// layout (DemoGlobal.h, 14 x 10 hubs, permanent edges at the box border).
// Build/run: see the ewlattice_test line of run_native_tests.sh.
//
// Checks:
//   * the offset table: 41 distinct offsets, one per residue
//   * centerOf / siteOf / isCenter agree, and the EWs tile the plane
//   * CenterIter yields exactly the centers of a rectangle, row-major
//   * every grid site has exactly one owning hub (center rule)
//   * per cache condition: every center is in exactly one hub's event
//     area; a hub's area is its own kept EWs plus one band per existing
//     neighbor; each band is exactly the EWs the neighbor owns inside
//     the borrower's area (brute force), in the same order at both ends
//   * every stored site (owned or borrowed EW) lies in the hub's
//     storeBox
// and prints the EW counts (owned, borrowed per condition) and sizes.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>

#include "EWLattice.h"
#include "S8C.h"

using namespace MFM;

namespace {

int g_bad = 0;

void check(bool ok, const char * what) {
  if (!ok) {
    if (g_bad < 20) printf("FAIL: %s\n", what);
    ++g_bad;
  }
}

typedef EWLattice L;

constexpr s32 HX = (s32) (DG::BLACKHOLE_ACTIVE_HUB_WIDTH *
                          DG::BLACKHOLE_CHIP_ARRAY_WIDTH);
constexpr s32 HY = (s32) (DG::BLACKHOLE_ACTIVE_HUB_HEIGHT *
                          DG::BLACKHOLE_CHIP_ARRAY_HEIGHT);
constexpr s32 GW = (s32) DG::ACTIVE_GRID_WIDTH;
constexpr s32 GH = (s32) DG::ACTIVE_GRID_HEIGHT;
constexpr s32 BW = (s32) DG::T6GRID_BASE_WIDTH;
constexpr s32 BH = (s32) DG::T6GRID_BASE_HEIGHT;

u8 permEdgesOf(s32 hx, s32 hy) {
  u8 pe = 0;
  if (hx == 0) pe |= (u8) (1u << D4_W);
  if (hx == HX - 1) pe |= (u8) (1u << D4_E);
  if (hy == 0) pe |= (u8) (1u << D4_N);
  if (hy == HY - 1) pe |= (u8) (1u << D4_S);
  return pe;
}

L::Rect hubRectOf(s32 h) {
  const s32 hx = h % HX;
  const s32 hy = h / HX;
  return L::hubRect(hx, hy, permEdgesOf(hx, hy));
}

// The owning hub of a center, by searching every hub (independent of the
// rectangle arithmetic the bands use).
s32 ownerOf(L::Pt c) {
  s32 found = -1;
  for (s32 h = 0; h < HX * HY; ++h) {
    if (hubRectOf(h).contains(c)) {
      check(found < 0, "center owned twice");
      found = h;
    }
  }
  return found;
}

// Centers whose EW has at least one site on the grid.
bool ewTouchesGrid(L::Pt c) {
  for (u32 k = 0; k < (u32) L::SITES; ++k)
    if (L::onGrid(L::siteOf(c, k))) return true;
  return false;
}

void testOffsets() {
  bool seen[L::SITES] = {};
  for (s32 r = 0; r < L::SITES; ++r) {
    const s32 dx = L::OFFSETS.dx[r];
    const s32 dy = L::OFFSETS.dy[r];
    check((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= L::RADIUS,
          "offset outside the EW");
    check(L::mod(L::KX * dx + L::KY * dy) == r, "offset residue");
    seen[r] = true;
  }
  s32 n = 0;
  for (s32 r = 0; r < L::SITES; ++r) n += seen[r];
  check(n == L::SITES, "offset table incomplete");
  // distinct offsets
  for (s32 a = 0; a < L::SITES; ++a)
    for (s32 b = a + 1; b < L::SITES; ++b)
      check(L::OFFSETS.dx[a] != L::OFFSETS.dx[b] ||
            L::OFFSETS.dy[a] != L::OFFSETS.dy[b], "offset repeated");
}

void testTiling() {
  const s32 lo = -120;
  const s32 hi = 120;
  const s32 n = hi - lo;
  std::vector<int> cover((size_t) (n * n), 0);
  for (s32 y = lo; y < hi; ++y) {
    for (s32 x = lo; x < hi; ++x) {
      const L::Pt c = L::centerOf(x, y);
      check(L::isCenter(c.x, c.y), "centerOf is not a center");
      const L::Pt s = L::siteOf(c, (u32) L::residue(x, y));
      check(s.x == x && s.y == y, "siteOf(centerOf) round trip");
      if (L::isCenter(x, y)) {
        for (u32 k = 0; k < (u32) L::SITES; ++k) {
          const L::Pt p = L::siteOf(L::Pt{x, y}, k);
          if (p.x >= lo && p.x < hi && p.y >= lo && p.y < hi)
            ++cover[(size_t) ((p.y - lo) * n + (p.x - lo))];
        }
      }
    }
  }
  // interior sites (away from the box edge) are covered exactly once
  for (s32 y = lo + L::RADIUS; y < hi - L::RADIUS; ++y)
    for (s32 x = lo + L::RADIUS; x < hi - L::RADIUS; ++x)
      check(cover[(size_t) ((y - lo) * n + (x - lo))] == 1,
            "EWs do not tile the plane");
}

void testIter() {
  std::mt19937 rng(12345);
  std::uniform_int_distribution<s32> pos(-300, 300);
  std::uniform_int_distribution<s32> len(0, 90);
  for (int t = 0; t < 2000; ++t) {
    L::Rect r;
    r.x0 = pos(rng);
    r.y0 = pos(rng);
    r.x1 = r.x0 + len(rng) - 5;
    r.y1 = r.y0 + len(rng) - 5;
    std::vector<L::Pt> want;
    for (s32 y = r.y0; y < r.y1; ++y)
      for (s32 x = r.x0; x < r.x1; ++x)
        if (L::isCenter(x, y)) want.push_back(L::Pt{x, y});
    std::vector<L::Pt> got;
    for (L::CenterIter it(r); !it.done(); it.next()) got.push_back(it.get());
    bool same = want.size() == got.size();
    for (size_t i = 0; same && i < want.size(); ++i)
      same = want[i].x == got[i].x && want[i].y == got[i].y;
    check(same, "CenterIter differs from brute force");
    check(L::countCenters(r) == (u32) want.size(), "countCenters");
  }
}

void testOwnership() {
  std::vector<s32> owned((size_t) (HX * HY), 0);
  for (s32 y = 0; y < GH; ++y) {
    for (s32 x = 0; x < GW; ++x) {
      const s32 h = ownerOf(L::centerOf(x, y));
      check(h >= 0, "grid site with no owner");
      if (h >= 0) ++owned[(size_t) h];
    }
  }
  long total = 0;
  for (s32 h = 0; h < HX * HY; ++h) total += owned[(size_t) h];
  check(total == (long) GW * GH, "owned sites do not add up to the grid");
  s32 lo = 1 << 30;
  s32 hi = 0;
  for (s32 hy = 1; hy < HY - 1; ++hy) {
    for (s32 hx = 1; hx < HX - 1; ++hx) {
      const s32 n = owned[(size_t) (hy * HX + hx)];
      lo = n < lo ? n : lo;
      hi = n > hi ? n : hi;
    }
  }
  check(lo % L::SITES == 0 && hi % L::SITES == 0,
        "interior hub owns a partial EW");
  printf("owned per interior hub: %d..%d sites = %d..%d EWs "
         "(SELF %d)\n", lo, hi, lo / L::SITES, hi / L::SITES, BW * BH);
}

void testConditions() {
  const Corner4 corners[] = {C4_SE, C4_SW, C4_NW, C4_NE};
  // centers that matter: every EW with a site on the grid
  std::vector<L::Pt> centers;
  for (s32 y = -L::RADIUS; y < GH + L::RADIUS; ++y)
    for (s32 x = -L::RADIUS; x < GW + L::RADIUS; ++x)
      if (L::isCenter(x, y) && ewTouchesGrid(L::Pt{x, y}))
        centers.push_back(L::Pt{x, y});
  std::vector<s32> owners;
  for (const L::Pt & p : centers) owners.push_back(ownerOf(p));
  s32 blo = 1 << 30;
  s32 bhi = 0;
  for (Corner4 c : corners) {
    // each center in exactly one hub's event area
    for (const L::Pt & p : centers) {
      int n = 0;
      for (s32 h = 0; h < HX * HY; ++h)
        n += L::eventArea(hubRectOf(h), c).contains(p);
      check(n == 1, "center in zero or several event areas");
    }
    for (s32 h = 0; h < HX * HY; ++h) {
      const s32 hx = h % HX;
      const s32 hy = h / HX;
      const L::Rect rh = hubRectOf(h);
      const L::Rect area = L::eventArea(rh, c);
      const L::Rect box = L::storeBox(hx * BW, hy * BH);
      // area = kept own EWs + one band per existing neighbor
      long pieces = 0;
      for (L::CenterIter it(L::intersect(rh, area)); !it.done(); it.next())
        if (ewTouchesGrid(it.get())) ++pieces;
      s32 borrowed = 0;
      for (s32 d8 = 0; d8 < 8; ++d8) {
        const s32 nx = hx + S8C::makeS8CFromDir8((Dir8) d8).x;
        const s32 ny = hy + S8C::makeS8CFromDir8((Dir8) d8).y;
        if (nx < 0 || ny < 0 || nx >= HX || ny >= HY) continue;
        const s32 n = ny * HX + nx;
        // borrower's view: the band rectangle, walked in order
        std::vector<L::Pt> mine;
        for (L::CenterIter it(L::band(rh, hubRectOf(n), c)); !it.done();
             it.next())
          if (ewTouchesGrid(it.get())) mine.push_back(it.get());
        // lender's view, brute force: centers n owns inside h's area
        std::vector<L::Pt> theirs;
        for (size_t i = 0; i < centers.size(); ++i)
          if (owners[i] == n && area.contains(centers[i]))
            theirs.push_back(centers[i]);
        bool same = mine.size() == theirs.size();
        for (size_t i = 0; same && i < mine.size(); ++i)
          same = mine[i].x == theirs[i].x && mine[i].y == theirs[i].y;
        check(same, "band differs between borrower and lender");
        pieces += (long) mine.size();
        borrowed += (s32) mine.size();
        // every borrowed site, if on the grid, is stored by both ends
        const L::Rect nbox = L::storeBox(nx * BW, ny * BH);
        for (const L::Pt & p : mine) {
          for (u32 k = 0; k < (u32) L::SITES; ++k) {
            const L::Pt s = L::siteOf(p, k);
            if (!L::onGrid(s)) continue;
            check(box.contains(s), "borrowed site outside storeBox");
            check(nbox.contains(s), "lent site outside lender storeBox");
          }
        }
      }
      long inArea = 0;
      for (const L::Pt & p : centers) inArea += area.contains(p);
      check(pieces == inArea, "area is not own + bands");
      if (hx > 0 && hy > 0 && hx < HX - 1 && hy < HY - 1) {
        blo = borrowed < blo ? borrowed : blo;
        bhi = borrowed > bhi ? borrowed : bhi;
      }
    }
  }
  // owned EWs' sites stay in the storeBox too
  for (size_t i = 0; i < centers.size(); ++i) {
    const L::Pt & p = centers[i];
    const s32 h = owners[i];
    const L::Rect box = L::storeBox((h % HX) * BW, (h / HX) * BH);
    for (u32 k = 0; k < (u32) L::SITES; ++k) {
      const L::Pt s = L::siteOf(p, k);
      if (L::onGrid(s)) check(box.contains(s), "owned site outside box");
    }
  }
  const L::Rect b0 = L::storeBox(0, 0);
  printf("borrowed per interior hub per condition: %d..%d EWs "
         "(SHIFT %d)\n", blo, bhi, (int) L::SHIFT);
  printf("storeBox %d x %d = %d sites (today's T6Grid %d x %d = %d)\n",
         b0.x1 - b0.x0, b0.y1 - b0.y0, (b0.x1 - b0.x0) * (b0.y1 - b0.y0),
         (int) DG::T6GRID_WIDTH, (int) DG::T6GRID_HEIGHT,
         (int) (DG::T6GRID_WIDTH * DG::T6GRID_HEIGHT));
  printf("EWs touching the grid: %zu\n", centers.size());
}

}  // namespace

int main() {
  testOffsets();
  testTiling();
  testIter();
  testOwnership();
  testConditions();
  if (g_bad) {
    printf("ewlattice_test: %d FAILED\n", g_bad);
    return 1;
  }
  printf("ewlattice_test: all checks passed\n");
  return 0;
}
