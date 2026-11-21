// Host test for CachingPolicy.h.  Build and run:
//   g++ -std=c++17 -DBUILD_HOST -Isrcs/shared/include -Isrcs/host/include \
//     scripts/cachepolicy/cachepolicy_test.cpp -o /tmp/cpt && /tmp/cpt
// Checks, for every policy and every argument, on a 4x4 array of hubs:
//   * the two ends of every pair agree: SEND_SITES <-> RECV_CACHES, or
//     NONE <-> NONE
//   * the event windows of different hubs never overlap, in global sites
//   * a hub's events never touch a band it sends, nor a cache it does not
//     receive, and stay inside the grid
//   * FOURCORNERS: every hub receives exactly 3 directions and sends 3
#include "CachingPolicy.h"
#include <cstdio>
#include <cstdlib>

// the headers' assertions call this; a host test never gets there
extern "C" void DieHereNow(signed, const char *, unsigned, const char *) {
  printf("FAIL DieHereNow\n");
  _Exit(2);
}

using namespace MFM;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

struct Rect { int x0, y0, x1, y1; };   // [x0,x1) x [y0,y1)

static bool overlap(const Rect & a, const Rect & b) {
  return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static Rect ofRange(const U8CRange & r) {
  return { r.start.x, r.start.y, r.stop.x, r.stop.y };
}

static const int DX[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };   // Dir8: NT NW WT SW ST SE ET NE
static const int DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static PolicyCtx ctxFor(u8 arg, int hx, int hy, int nx, int ny) {
  PolicyCtx c;
  c.weLead = (arg == (u8) ((hx & 1) | ((hy & 1) << 1)));
  c.peerLeads = (arg == (u8) ((nx & 1) | ((ny & 1) << 1)));
  c.noLeader = false;
  return c;
}

static Rect reach(const EventBounds & b) {   // sites an event window can touch
  const int R = CachingPolicyBase<SuperCellPolicy>::WINDOW_RADIUS;
  return { b.x0 - R, b.y0 - R, b.x1 - 1 + R + 1, b.y1 - 1 + R + 1 };
}

int main() {
  const int N = 4;
  const int BW = NgbCacheMgr::BW, BH = NgbCacheMgr::BH;
  for (int pol = 0; pol < CP_COUNT; ++pol) {
    CachePolicyId id = (CachePolicyId) pol;
    for (u8 arg = 0; arg < 4; ++arg) {
      for (int hy = 0; hy < N; ++hy) for (int hx = 0; hx < N; ++hx) {
        // our event reach, in global sites
        PolicyCtx me = ctxFor(arg, hx, hy, hx, hy);
        EventBounds eb = CachingPolicies::bounds(id, arg, me);
        Rect mine = reach(eb);
        CHECK(mine.x0 >= 0 && mine.y0 >= 0 && mine.x1 <= NgbCacheMgr::TW &&
              mine.y1 <= NgbCacheMgr::TH,
              "%s arg %u hub %d,%d reach outside the grid",
              cachePolicyName(id), arg, hx, hy);
        int recv = 0, send = 0;
        for (int d = 0; d < 8; ++d) {
          int nx = hx + DX[d], ny = hy + DY[d];
          PolicyCtx c = ctxFor(arg, hx, hy, nx, ny);
          DirPlan p = CachingPolicies::plan(id, arg, (Dir8) d, c);
          recv += p == DirPlan::RECV_CACHES;
          send += p == DirPlan::SEND_SITES;
          if (nx >= 0 && nx < N && ny >= 0 && ny < N) {
            PolicyCtx cn = ctxFor(arg, nx, ny, hx, hy);
            DirPlan q = CachingPolicies::plan(id, arg, oppositeDir8((Dir8) d), cn);
            bool ok = (p == DirPlan::SEND_SITES && q == DirPlan::RECV_CACHES) ||
                      (p == DirPlan::RECV_CACHES && q == DirPlan::SEND_SITES) ||
                      (p == DirPlan::NONE && q == DirPlan::NONE);
            CHECK(ok, "%s arg %u hubs %d,%d / %d,%d dir %d disagree (%d vs %d)",
                  cachePolicyName(id), arg, hx, hy, nx, ny, d, (int) p, (int) q);
            // the neighbor's reach, in OUR coordinates
            EventBounds nb = CachingPolicies::bounds(id, arg,
                                                     ctxFor(arg, nx, ny, nx, ny));
            Rect theirs = reach(nb);
            theirs.x0 += DX[d] * BW; theirs.x1 += DX[d] * BW;
            theirs.y0 += DY[d] * BH; theirs.y1 += DY[d] * BH;
            CHECK(!overlap(mine, theirs),
                  "%s arg %u hubs %d,%d / %d,%d event windows overlap",
                  cachePolicyName(id), arg, hx, hy, nx, ny);
          }
          if (p == DirPlan::SEND_SITES)
            CHECK(!overlap(mine, ofRange(NgbCacheMgr::SITE_RANGES[d])),
                  "%s arg %u hub %d,%d events reach the band it sends (dir %d)",
                  cachePolicyName(id), arg, hx, hy, d);
          if (p == DirPlan::NONE)
            CHECK(!overlap(mine, ofRange(NgbCacheMgr::CACHE_RANGES[d])),
                  "%s arg %u hub %d,%d events reach an unexchanged cache (dir %d)",
                  cachePolicyName(id), arg, hx, hy, d);
        }
        if (id == CP_FOURCORNERS)
          CHECK(recv == 3 && send == 3,
                "fourcorners arg %u hub %d,%d recv %d send %d (want 3, 3)",
                arg, hx, hy, recv, send);
      }
    }
  }
  // CACHE ELISION: what may stay put when the corner changes prev -> next.
  // Same corner keeps all 6 exchanged directions, adjacent corners keep 2
  // (one borrowed band and the matching lent band), opposite corners none;
  // the two ends of a link agree (a kept direction's opposite is kept); the
  // supercell policy keeps nothing.
  for (u8 prev = 0; prev < 4; ++prev) for (u8 next = 0; next < 4; ++next) {
    int kept = 0;
    for (int d = 0; d < 8; ++d) {
      bool k = CachingPolicies::keepAcross(CP_FOURCORNERS, prev, next, (Dir8) d);
      bool o = CachingPolicies::keepAcross(CP_FOURCORNERS, prev, next,
                                           oppositeDir8((Dir8) d));
      kept += k;
      CHECK(k == o, "keep: the two ends of dir %d disagree (%u->%u)", d, prev, next);
      if (k) {
        PolicyCtx c = { false, false, false };
        DirPlan pp = CachingPolicies::plan(CP_FOURCORNERS, prev, (Dir8) d, c);
        DirPlan nn = CachingPolicies::plan(CP_FOURCORNERS, next, (Dir8) d, c);
        DirPlan qp = CachingPolicies::plan(CP_FOURCORNERS, prev, oppositeDir8((Dir8) d), c);
        CHECK(pp == nn && pp != DirPlan::NONE, "keep: role changed on dir %d", d);
        CHECK(qp != DirPlan::NONE && qp != pp,
              "keep: dir %d peer end not the opposite role (%u->%u)", d, prev, next);
      }
      CHECK(!CachingPolicies::keepAcross(CP_SUPERCELL, prev, next, (Dir8) d),
            "supercell must keep nothing");
    }
    int want = prev == next ? 6 : ((prev + 2) & 3) == next ? 0 : 2;
    CHECK(kept == want, "keep %u->%u kept %d dirs (want %d)", prev, next, kept, want);
  }
  // before the first argument both policies stay clear of every cache
  PolicyCtx none = { false, false, true };
  for (int pol = 0; pol < CP_COUNT; ++pol) {
    Rect r = reach(CachingPolicies::bounds((CachePolicyId) pol, 9, none));
    for (int d = 0; d < 8; ++d)
      CHECK(!overlap(r, ofRange(NgbCacheMgr::CACHE_RANGES[d])),
            "no-leader bounds reach cache %d", d);
  }
  // host flag round trip
  for (int pol = 0; pol < CP_COUNT; ++pol)
    CHECK(cachePolicyFromHostFlags(cachePolicyToHostFlags((CachePolicyId) pol) | 0x0f) == pol,
          "host flag round trip %d", pol);
  CHECK(cachePolicyFromHostFlags(0x0f) == CP_SUPERCELL, "default is supercell");
  CHECK(cachePolicyFromHostFlags(0x30) == CP_SUPERCELL, "unknown id is supercell");
  printf(fails ? "FAILURES: %d\n" : "PASS (all invariants)\n", fails);
  return fails != 0;
}
