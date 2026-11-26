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
//   * HUB SIDE, for every legal chip layout (1, 2 and 4 chips): a hub's
//     assumed neighbor rectangles give the same bands as the true ones;
//     borrower and lender BandWalks yield the same global sites in the
//     same order with the same ordinals, inside both stores; own kept EWs
//     plus the bands cover exactly the hub's accessible area; the center
//     tables pick exactly the brute-force live centers
// and prints the EW counts (owned, borrowed per condition) and sizes.
//
// ewlattice_test --dump FILE  instead writes, for scripts/geomlab, the
// header's view of every grid site: magic "EWL1", u32 width, u32 height,
// then five s16 arrays (row-major): the owning hub, and the hub whose
// event area holds the site in Corner4 0..3 (SE, SW, NW, NE).
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

constexpr s32 HX = (s32) (DG::BLACKHOLE_ACTIVE_HUB_WIDTH *
                          DG::BLACKHOLE_CHIP_ARRAY_WIDTH);
constexpr s32 HY = (s32) (DG::BLACKHOLE_ACTIVE_HUB_HEIGHT *
                          DG::BLACKHOLE_CHIP_ARRAY_HEIGHT);
constexpr s32 GW = (s32) DG::ACTIVE_GRID_WIDTH;
constexpr s32 GH = (s32) DG::ACTIVE_GRID_HEIGHT;
constexpr s32 BW = (s32) DG::T6GRID_BASE_WIDTH;
constexpr s32 BH = (s32) DG::T6GRID_BASE_HEIGHT;

constexpr s32 CHX = (s32) DG::BLACKHOLE_ACTIVE_HUB_WIDTH;
constexpr s32 CHY = (s32) DG::BLACKHOLE_ACTIVE_HUB_HEIGHT;

struct Layout {
  const char * name;
  u32 chips;                    //< bit per chip present
};

const Layout LAYOUTS[] = {
  {"0123", 0xfu},
  {"01", 0x3u},
  {"23", 0xcu},
  {"02", 0x5u},
  {"13", 0xau},
  {"0", 0x1u},
  {"1", 0x2u},
  {"2", 0x4u},
  {"3", 0x8u},
};

template <s32 T>
struct Suite {
  typedef EWLatticeT<T> L;
  typedef EWGeom::Pt Pt;
  typedef EWGeom::Rect Rect;
  typedef EWGeom::CenterTable CenterTable;
  typedef typename L::CenterIter CenterIter;
  typedef typename L::BandWalk BandWalk;

  static u8 permEdgesOf(s32 hx, s32 hy) {
    u8 pe = 0;
    if (hx == 0) pe |= (u8) (1u << D4_W);
    if (hx == HX - 1) pe |= (u8) (1u << D4_E);
    if (hy == 0) pe |= (u8) (1u << D4_N);
    if (hy == HY - 1) pe |= (u8) (1u << D4_S);
    return pe;
  }

  static Rect hubRectOf(s32 h) {
    const s32 hx = h % HX;
    const s32 hy = h / HX;
    return L::hubRect(hx, hy, permEdgesOf(hx, hy));
  }

  // The owning hub of a center, by searching every hub (independent of the
  // rectangle arithmetic the bands use).
  static s32 ownerOf(Pt c) {
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
  static bool ewTouchesGrid(Pt c) {
    for (u32 k = 0; k < (u32) L::SITES; ++k)
      if (L::onGrid(L::siteOf(c, k))) return true;
    return false;
  }

  static void testOffsets() {
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

  static void testTiling() {
    const s32 lo = -120;
    const s32 hi = 120;
    const s32 n = hi - lo;
    std::vector<int> cover((size_t) (n * n), 0);
    for (s32 y = lo; y < hi; ++y) {
      for (s32 x = lo; x < hi; ++x) {
        const Pt c = L::centerOf(x, y);
        check(L::isCenter(c.x, c.y), "centerOf is not a center");
        const Pt s = L::siteOf(c, (u32) L::residue(x, y));
        check(s.x == x && s.y == y, "siteOf(centerOf) round trip");
        if (L::isCenter(x, y)) {
          for (u32 k = 0; k < (u32) L::SITES; ++k) {
            const Pt p = L::siteOf(Pt{x, y}, k);
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

  static void testIter() {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<s32> pos(-300, 300);
    std::uniform_int_distribution<s32> len(0, 90);
    for (int t = 0; t < 2000; ++t) {
      Rect r;
      r.x0 = pos(rng);
      r.y0 = pos(rng);
      r.x1 = r.x0 + len(rng) - 5;
      r.y1 = r.y0 + len(rng) - 5;
      std::vector<Pt> want;
      for (s32 y = r.y0; y < r.y1; ++y)
        for (s32 x = r.x0; x < r.x1; ++x)
          if (L::isCenter(x, y)) want.push_back(Pt{x, y});
      std::vector<Pt> got;
      for (CenterIter it(r); !it.done(); it.next()) got.push_back(it.get());
      bool same = want.size() == got.size();
      for (size_t i = 0; same && i < want.size(); ++i)
        same = want[i].x == got[i].x && want[i].y == got[i].y;
      check(same, "CenterIter differs from brute force");
      check(L::countCenters(r) == (u32) want.size(), "countCenters");
    }
  }

  static void testOwnership() {
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
    printf("owned per interior hub: %d..%d sites = %d..%d tiles "
           "(SELF %d)\n", lo, hi, lo / L::SITES, hi / L::SITES, BW * BH);
  }

  static void testConditions() {
    const Corner4 corners[] = {C4_SE, C4_SW, C4_NW, C4_NE};
    // centers that matter: every EW with a site on the grid
    std::vector<Pt> centers;
    for (s32 y = -L::RADIUS; y < GH + L::RADIUS; ++y)
      for (s32 x = -L::RADIUS; x < GW + L::RADIUS; ++x)
        if (L::isCenter(x, y) && ewTouchesGrid(Pt{x, y}))
          centers.push_back(Pt{x, y});
    std::vector<s32> owners;
    for (const Pt & p : centers) owners.push_back(ownerOf(p));
    s32 blo = 1 << 30;
    s32 bhi = 0;
    for (Corner4 c : corners) {
      // each center in exactly one hub's event area
      for (const Pt & p : centers) {
        int n = 0;
        for (s32 h = 0; h < HX * HY; ++h)
          n += L::eventArea(hubRectOf(h), c).contains(p);
        check(n == 1, "center in zero or several event areas");
      }
      for (s32 h = 0; h < HX * HY; ++h) {
        const s32 hx = h % HX;
        const s32 hy = h / HX;
        const Rect rh = hubRectOf(h);
        const Rect area = L::eventArea(rh, c);
        const Rect box = L::storeBox(hx * BW, hy * BH);
        // area = kept own EWs + one band per existing neighbor
        long pieces = 0;
        for (CenterIter it(L::intersect(rh, area)); !it.done(); it.next())
          if (ewTouchesGrid(it.get())) ++pieces;
        s32 borrowed = 0;
        for (s32 d8 = 0; d8 < 8; ++d8) {
          const s32 nx = hx + S8C::makeS8CFromDir8((Dir8) d8).x;
          const s32 ny = hy + S8C::makeS8CFromDir8((Dir8) d8).y;
          if (nx < 0 || ny < 0 || nx >= HX || ny >= HY) continue;
          const s32 n = ny * HX + nx;
          // borrower's view: the band rectangle, walked in order
          std::vector<Pt> mine;
          for (CenterIter it(L::band(rh, hubRectOf(n), c)); !it.done();
               it.next())
            if (ewTouchesGrid(it.get())) mine.push_back(it.get());
          // lender's view, brute force: centers n owns inside h's area
          std::vector<Pt> theirs;
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
          const Rect nbox = L::storeBox(nx * BW, ny * BH);
          for (const Pt & p : mine) {
            for (u32 k = 0; k < (u32) L::SITES; ++k) {
              const Pt s = L::siteOf(p, k);
              if (!L::onGrid(s)) continue;
              check(box.contains(s), "borrowed site outside storeBox");
              check(nbox.contains(s), "lent site outside lender storeBox");
            }
          }
        }
        long inArea = 0;
        for (const Pt & p : centers) inArea += area.contains(p);
        check(pieces == inArea, "area is not own + bands");
        if (hx > 0 && hy > 0 && hx < HX - 1 && hy < HY - 1) {
          blo = borrowed < blo ? borrowed : blo;
          bhi = borrowed > bhi ? borrowed : bhi;
        }
      }
    }
    // owned EWs' sites stay in the storeBox too
    for (size_t i = 0; i < centers.size(); ++i) {
      const Pt & p = centers[i];
      const s32 h = owners[i];
      const Rect box = L::storeBox((h % HX) * BW, (h / HX) * BH);
      for (u32 k = 0; k < (u32) L::SITES; ++k) {
        const Pt s = L::siteOf(p, k);
        if (L::onGrid(s)) check(box.contains(s), "owned site outside box");
      }
    }
    const Rect b0 = L::storeBox(0, 0);
    printf("borrowed per interior hub per condition: %d..%d tiles "
           "(SHIFT %d)\n", blo, bhi, (int) L::SHIFT);
    printf("storeBox %d x %d = %d sites (today's T6Grid %d x %d = %d)\n",
           b0.x1 - b0.x0, b0.y1 - b0.y0, (b0.x1 - b0.x0) * (b0.y1 - b0.y0),
           (int) DG::T6GRID_WIDTH, (int) DG::T6GRID_HEIGHT,
           (int) (DG::T6GRID_WIDTH * DG::T6GRID_HEIGHT));
    printf("tiles touching the grid: %zu\n", centers.size());
  }

  // The hub (index) among the 5 x 5 hubs around p whose rectangle rectOf(h)
  // contains p (exactly one is expected).
  template <class F>
  static s32 findHub(Pt p, F rectOf) {
    s32 cx = p.x / BW;
    s32 cy = p.y / BH;
    s32 found = -1;
    for (s32 dy = -2; dy <= 2; ++dy) {
      for (s32 dx = -2; dx <= 2; ++dx) {
        const s32 hx = cx + dx;
        const s32 hy = cy + dy;
        if (hx < 0 || hy < 0 || hx >= HX || hy >= HY) continue;
        const s32 h = hy * HX + hx;
        if (rectOf(h).contains(p)) {
          check(found < 0, "dump: two hubs claim a center");
          found = h;
        }
      }
    }
    check(found >= 0, "dump: no hub claims a center");
    return found;
  }

  static int dump(const char * path) {
    FILE * f = fopen(path, "wb");
    if (!f) {
      printf("cannot write %s\n", path);
      return 1;
    }
    const Corner4 corners[] = {C4_SE, C4_SW, C4_NW, C4_NE};
    const u32 dims[2] = {(u32) GW, (u32) GH};
    fwrite("EWL1", 1, 4, f);
    fwrite(dims, sizeof(u32), 2, f);
    std::vector<s16> row((size_t) GW);
    for (int k = -1; k < 4; ++k) {
      for (s32 y = 0; y < GH; ++y) {
        for (s32 x = 0; x < GW; ++x) {
          const Pt c = L::centerOf(x, y);
          s32 h;
          if (k < 0) {
            h = findHub(c, [](s32 i) { return hubRectOf(i); });
          } else {
            const Corner4 c4 = corners[k];
            h = findHub(c, [c4](s32 i) {
              return L::eventArea(hubRectOf(i), c4);
            });
          }
          row[(size_t) x] = (s16) h;
        }
        fwrite(row.data(), sizeof(s16), row.size(), f);
      }
    }
    fclose(f);
    printf("ewlattice dump: %s (%d x %d, owner + 4 conditions)%s\n", path,
           (int) GW, (int) GH, g_bad ? ", WITH FAILURES" : "");
    return g_bad ? 1 : 0;
  }

  // ---- hub side (step 3) over the legal chip layouts ----

  static constexpr s32 SW = BW + 2 * L::STORE_MARGIN;   // store width
  static constexpr s32 SH = BH + 2 * L::STORE_MARGIN;   // store height

  static bool present(const Layout & lay, s32 hx, s32 hy) {
    if (hx < 0 || hy < 0 || hx >= HX || hy >= HY) return false;
    const u32 chip = (u32) ((hy / CHY) * 2 + (hx / CHX));
    return (lay.chips >> chip) & 1u;
  }

  static u8 permOf(const Layout & lay, s32 hx, s32 hy) {
    u8 pe = 0;
    if (!present(lay, hx - 1, hy)) pe |= (u8) (1u << D4_W);
    if (!present(lay, hx + 1, hy)) pe |= (u8) (1u << D4_E);
    if (!present(lay, hx, hy - 1)) pe |= (u8) (1u << D4_N);
    if (!present(lay, hx, hy + 1)) pe |= (u8) (1u << D4_S);
    return pe;
  }

  static Pt local0Of(s32 hx, s32 hy) {
    return Pt{hx * BW - L::STORE_MARGIN, hy * BH - L::STORE_MARGIN};
  }

  static std::vector<Pt> centersIn(const Rect & r) {
    std::vector<Pt> v;
    for (CenterIter it(r); !it.done(); it.next()) v.push_back(it.get());
    return v;
  }

  struct WalkSite {
    s32 gx;
    s32 gy;
    u16 ord;
  };

  static std::vector<WalkSite> walk(const Rect & band, const Rect & acc,
                             Pt local0) {
    std::vector<WalkSite> v;
    BandWalk bw;
    bw.init(band, acc, local0);
    for (; !bw.atEnd(); bw.next()) {
      const Pt l = bw.localSite();
      check(l.x >= 0 && l.y >= 0 && l.x < SW && l.y < SH,
            "walk site outside the store");
      const Pt g = bw.globalSite();
      check(g.x - local0.x == l.x && g.y - local0.y == l.y, "local coords");
      v.push_back(WalkSite{g.x, g.y, bw.ordinal()});
    }
    return v;
  }

  static void testHubSide() {
    const Corner4 corners[] = {C4_SE, C4_SW, C4_NW, C4_NE};
    std::vector<u8> scratch((size_t) (2 * L::bitBytes(SW, SH)));
    u32 maxRuns = 0;
    u32 minLive = 1u << 30;
    u32 maxLive = 0;
    long tables = 0;
    std::vector<u8> everLive((size_t) (GW * GH), 0);
    for (const Layout & lay : LAYOUTS) {
      for (s32 hy = 0; hy < HY; ++hy) {
        for (s32 hx = 0; hx < HX; ++hx) {
          if (!present(lay, hx, hy)) continue;
          const u8 pe = permOf(lay, hx, hy);
          const Rect rh = L::hubRect(hx, hy, pe);
          const Rect acc = L::accessibleRect(hx, hy, pe);
          const Pt l0 = local0Of(hx, hy);
          const bool doTables =
            lay.chips != 0xfu || ((hx + hy) % 3) == 0;   // sample the box
          for (int ci = -1; ci < 4; ++ci) {
            const Rect area =
              ci < 0 ? rh : L::eventArea(rh, corners[ci]);
            // coverage: kept own tiles + bands == accessible area sites
            std::vector<u8> seen((size_t) (SW * SH), 0);
            auto mark = [&](const std::vector<WalkSite> & v) {
              for (const WalkSite & w : v) {
                const s32 lx = w.gx - l0.x;
                const s32 ly = w.gy - l0.y;
                if (lx < 0 || ly < 0 || lx >= SW || ly >= SH) continue;
                const s32 i = ly * SW + lx;
                check(seen[(size_t) i] == 0, "site walked twice");
                seen[(size_t) i] = 1;
              }
            };
            mark(walk(L::intersect(rh, area), acc, l0));
            for (s32 d8 = 0; ci >= 0 && d8 < 8; ++d8) {
              const S8C dv = S8C::makeS8CFromDir8((Dir8) d8);
              const s32 nx = hx + dv.x;
              const s32 ny = hy + dv.y;
              if (!present(lay, nx, ny)) continue;
              const u8 pn = permOf(lay, nx, ny);
              const Rect rn = L::hubRect(nx, ny, pn);
              const Rect rnAssumed =
                L::hubRect(nx, ny, L::neighborPermEdges(pe, dv.x, dv.y));
              const Rect bTrue = L::band(rh, rn, corners[ci]);
              const Rect bMine = L::band(rh, rnAssumed, corners[ci]);
              const std::vector<Pt> ct = centersIn(bTrue);
              const std::vector<Pt> cm = centersIn(bMine);
              bool same = ct.size() == cm.size();
              for (size_t i = 0; same && i < ct.size(); ++i)
                same = ct[i].x == cm[i].x && ct[i].y == cm[i].y;
              check(same, "assumed neighbor rect changes the band");
              // the lender's own view of the same band
              const Rect rhAssumed =
                L::hubRect(hx, hy, L::neighborPermEdges(pn, -dv.x, -dv.y));
              const Rect bLend = L::band(rhAssumed, rn, corners[ci]);
              const std::vector<WalkSite> wb = walk(bMine, acc, l0);
              const std::vector<WalkSite> wl =
                walk(bLend, L::accessibleRect(nx, ny, pn), local0Of(nx, ny));
              bool agree = wb.size() == wl.size();
              for (size_t i = 0; agree && i < wb.size(); ++i)
                agree = wb[i].gx == wl[i].gx && wb[i].gy == wl[i].gy &&
                  wb[i].ord == wl[i].ord;
              check(agree, "borrower and lender walk differently");
              mark(wb);
            }
            // what must be covered: accessible store sites, center in area
            for (s32 y = 0; y < SH; ++y) {
              for (s32 x = 0; x < SW; ++x) {
                const Pt g{l0.x + x, l0.y + y};
                const bool want =
                  acc.contains(g) && area.contains(L::centerOf(g.x, g.y));
                check(want == (seen[(size_t) (y * SW + x)] != 0),
                      "kept + bands != accessible area");
              }
            }
            const bool full = lay.chips == 0xfu && ci >= 0;
            if (!doTables && !full) continue;
            CenterTable t;
            const bool fit =
              L::buildCenterTable(t, area, acc, l0, SW, SH, scratch.data());
            check(fit, "center table overflow");
            if (!fit) continue;
            if (full) {                  // no site starved: union of live
              for (u32 r = 0; r < t.mTotal; ++r) {
                const Pt p = t.pick(r);
                const s32 gx = l0.x + p.x;
                const s32 gy = l0.y + p.y;
                check(L::onGrid(Pt{gx, gy}), "live center off the grid");
                if (L::onGrid(Pt{gx, gy}))
                  everLive[(size_t) (gy * GW + gx)] = 1;
              }
            }
            if (!doTables) continue;
            ++tables;
            maxRuns = t.mRuns > maxRuns ? t.mRuns : maxRuns;
            if (ci >= 0) {
              minLive = t.mTotal < minLive ? t.mTotal : minLive;
              maxLive = t.mTotal > maxLive ? t.mTotal : maxLive;
            }
            // brute force, row-major
            std::vector<Pt> live;
            for (s32 y = 0; y < SH; ++y) {
              for (s32 x = 0; x < SW; ++x) {
                const Pt g{l0.x + x, l0.y + y};
                bool ok = acc.contains(g) &&
                  area.contains(L::centerOf(g.x, g.y));
                for (s32 dy = -L::WINDOW_RADIUS; ok && dy <= L::WINDOW_RADIUS;
                     ++dy) {
                  for (s32 dx = -L::WINDOW_RADIUS;
                       ok && dx <= L::WINDOW_RADIUS; ++dx) {
                    if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) >
                        L::WINDOW_RADIUS) continue;
                    const Pt w{g.x + dx, g.y + dy};
                    ok = !acc.contains(w) ||
                      area.contains(L::centerOf(w.x, w.y));
                  }
                }
                if (ok) live.push_back(Pt{x, y});
              }
            }
            bool same = live.size() == t.mTotal;
            for (u32 r = 0; same && r < t.mTotal; ++r) {
              const Pt p = t.pick(r);
              same = p.x == live[r].x && p.y == live[r].y;
            }
            check(same, "center table differs from brute force");
          }
        }
      }
    }
    long starved = 0;
    for (size_t i = 0; i < everLive.size(); ++i) starved += !everLive[i];
    check(starved == 0, "a grid site is never a live center (starved)");
    printf("hub side: %zu layouts ok-checked; %ld center tables, max %u runs "
           "(capacity %u), live centers per corner %u..%u; starved sites "
           "(full box, 4 corners) %ld\n",
           sizeof(LAYOUTS) / sizeof(LAYOUTS[0]), tables, maxRuns,
           (u32) L::CT_MAX_RUNS, minLive, maxLive, starved);
  }

  static void run() {
    printf("---- tile radius T = %d (%d sites, SHIFT %d, store margin %d)\n",
           (int) T, (int) L::SITES, (int) L::SHIFT, (int) L::STORE_MARGIN);
    testOffsets();
    testTiling();
    testIter();
    testOwnership();
    testConditions();
    testHubSide();
  }

};

}  // namespace

int main(int argc, char ** argv) {
  if ((argc == 3 || argc == 4) && strcmp(argv[1], "--dump") == 0) {
    const int t = argc == 4 ? atoi(argv[3]) : 4;
    if (t == 4) return Suite<4>::dump(argv[2]);
    if (t == 8) return Suite<8>::dump(argv[2]);
    if (t == 10) return Suite<10>::dump(argv[2]);
    printf("--dump: tile radius 4, 8 or 10\n");
    return 1;
  }
  Suite<4>::run();
  Suite<8>::run();
  Suite<10>::run();
  if (g_bad) {
    printf("ewlattice_test: %d FAILED\n", g_bad);
    return 1;
  }
  printf("ewlattice_test: all checks passed\n");
  return 0;
}
