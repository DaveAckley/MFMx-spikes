// Host test for HubEdges.h and CachingPolicies::withPermanentEdges.
// Build and run:
//   g++ -std=c++17 -DBUILD_HOST -Isrcs/shared/include -Isrcs/host/include \
//     scripts/hubedges/hubedges_test.cpp srcs/shared/src/S8C.cpp -o /tmp/het && /tmp/het
// For each of the nine possible card layouts (0, 1, 2, 3, 01, 23, 02,
// 13, 0123), every hub of every chip must report a permanent edge on a
// side exactly when that side is on the chip border and has no seam.
// Then the event bounds of a permanent side must reach the last real
// site, and the other sides must keep the policy's bounds.
#include "CachingPolicy.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" void DieHereNow(signed, const char *, unsigned, const char *) {
  printf("FAIL DieHereNow\n");
  _Exit(2);
}

using namespace MFM;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

struct Seam { int a; Dir4 d; int b; };   //< chip b lies d of chip a

static const Dir4 OPP[4] = { D4_S, D4_E, D4_N, D4_W };  //< indexed by Dir4

static u16 maskFor(int chip, const std::vector<Seam> & seams) {
  u16 mask = 0u;
  for (const Seam & s : seams) {
    Dir4 d = D4_N;
    bool mine = false;
    if (s.a == chip) { d = s.d; mine = true; }
    if (s.b == chip) { d = OPP[s.d]; mine = true; }
    if (!mine) continue;
    u32 nib = (1u << EthSeam::cablingFor(d).n) - 1u;   // every link live
    mask = EthSeam::packSeamMask(d, nib, mask);
  }
  return mask;
}

static void checkLayout(const char * name, std::vector<int> chips,
                        std::vector<Seam> seams) {
  for (int chip : chips) {
    EthSeam::SeamTable t;
    EthSeam::decodeSeamMask(maskFor(chip, seams), t);
    for (u32 hy = 0; hy < 5; ++hy) {
      for (u32 hx = 0; hx < 7; ++hx) {
        U8C ct6(hx * 2 + 1, hy * 2 + 1);   // the hub is the cell's (1,1) tile
        u8 want = 0;
        if (hy == 0 && t.n[D4_N] == 0) want |= HubEdges::PE_N;
        if (hy == 4 && t.n[D4_S] == 0) want |= HubEdges::PE_S;
        if (hx == 0 && t.n[D4_W] == 0) want |= HubEdges::PE_W;
        if (hx == 6 && t.n[D4_E] == 0) want |= HubEdges::PE_E;
        u8 got = HubEdges::permanentEdges(t, ct6);
        CHECK(got == want, "%s chip %d hub %u,%u: edges %x want %x",
              name, chip, hx, hy, got, want);
      }
    }
  }
}

static u32 countEdgeHubs(std::vector<int> chips, std::vector<Seam> seams) {
  u32 n = 0;
  for (int chip : chips) {
    EthSeam::SeamTable t;
    EthSeam::decodeSeamMask(maskFor(chip, seams), t);
    for (u32 hy = 0; hy < 5; ++hy)
      for (u32 hx = 0; hx < 7; ++hx)
        if (HubEdges::permanentEdges(t, U8C(hx * 2 + 1, hy * 2 + 1))) ++n;
  }
  return n;
}

int main() {
  const Seam s01 = { 0, D4_E, 1 }, s23 = { 2, D4_E, 3 };
  const Seam s02 = { 0, D4_S, 2 }, s13 = { 1, D4_S, 3 };
  checkLayout("0", {0}, {});
  checkLayout("1", {1}, {});
  checkLayout("2", {2}, {});
  checkLayout("3", {3}, {});
  checkLayout("01", {0, 1}, {s01});
  checkLayout("23", {2, 3}, {s23});
  checkLayout("02", {0, 2}, {s02});
  checkLayout("13", {1, 3}, {s13});
  checkLayout("0123", {0, 1, 2, 3}, {s01, s23, s02, s13});

  // hubs on a border of a 7x5 chip: 2*7+2*3 = 20; two chips side by side
  // 14x5 -> 2*14+2*3 = 34 (E-W pair), 7x10 -> 2*7+2*8 = 30 (N-S pair),
  // 2x2 cards 14x10 -> 2*14+2*8 = 44
  CHECK(countEdgeHubs({0}, {}) == 20, "single chip edge hubs");
  CHECK(countEdgeHubs({0, 1}, {s01}) == 34, "EW pair edge hubs");
  CHECK(countEdgeHubs({0, 2}, {s02}) == 30, "NS pair edge hubs");
  CHECK(countEdgeHubs({0, 1, 2, 3}, {s01, s23, s02, s13}) == 44,
        "2x2 edge hubs");

  // bounds
  const EventBounds none = CachingPolicies::withPermanentEdges({4, 24, 134, 104}, 0);
  CHECK(none.x0 == 4 && none.y0 == 24 && none.x1 == 134 && none.y1 == 104,
        "no permanent edge leaves the bounds alone");
  const EventBounds w = CachingPolicies::withPermanentEdges({24, 24, 134, 104},
                                                           HubEdges::PE_W);
  CHECK(w.x0 == 10 && w.y0 == 24 && w.x1 == 134 && w.y1 == 104,
        "west edge: x0 is the first real site only");
  const EventBounds all = CachingPolicies::withPermanentEdges({4, 4, 154, 124},
      HubEdges::PE_N | HubEdges::PE_W | HubEdges::PE_S | HubEdges::PE_E);
  CHECK(all.x0 == 10 && all.y0 == 10 && all.x1 == 148 && all.y1 == 118,
        "all four edges: the whole self region");
  // every policy and argument: with a W edge the lowest centre is 10, the
  // other sides are untouched, and the other axis is unchanged
  for (u32 pol = 0; pol < CP_COUNT; ++pol) {
    for (u32 arg = 0; arg < 4; ++arg) {
      PolicyCtx pc;
      pc.weLead = true; pc.peerLeads = false; pc.noLeader = false;
      EventBounds b = CachingPolicies::bounds((CachePolicyId) pol, (u8) arg, pc);
      EventBounds e = CachingPolicies::withPermanentEdges(b, HubEdges::PE_E);
      CHECK(e.x1 == 148 && e.x0 == b.x0 && e.y0 == b.y0 && e.y1 == b.y1,
            "policy %u arg %u: east edge only changes x1", pol, arg);
    }
  }
  if (fails == 0) printf("PASS (all invariants)\n");
  return fails ? 1 : 0;
}
