// ethseam_test.cpp -- host-side checks of shared/include/EthSeam.h.
// Build/run (from srcs/host/tools):  g++ -std=c++17 -O2 -Wall -Wextra -Werror -I../../shared/include ethseam_test.cpp -o /tmp/ethseam_test && /tmp/ethseam_test
// Checks: layout constants equal what the E1 images and ethfwd14..20 use; for every cell geometry (2x2, 2x1, 1x2, 1x1), seam kind and live link
// count 1..4: the ordinals are a bijection, assign/invert round-trip, links are dealt in balance (counts differ by <= 1), and it prints the
// channels-per-link table with the fits-in-NCH_MAX verdicts used in ERISC_SEAM_DESIGN.md section 13.
#include "EthSeam.h"
#include <cstdio>
#include <cstdlib>
using namespace MFM::EthSeam;
using MFM::u32;

// ---- layout must equal the tools' copies (ethfwd14..20 / e1fwd13..16) ----
static_assert(NCH_MAX == 8 && CAR_BYTES == 0x1040 && RING_CARS == 4 && RING_BYTES == 0x4100 && CHUNK_BYTES == 1456, "ring geometry");
static_assert(E1_CODE == 0x4000 && CTL_BASE == 0x8000 && CTL_WINDOW == 0x600 && CHANNEL_BLOCK0 == 0x8100 && CHANNEL_BLOCK_STRIDE == 0x80, "CTL layout");
static_assert(OUT_RING0 == 0x10000 && IN_RING0 == 0x30800 && channelBlockAddr(3) == 0x8100 + 3 * 0x80, "ring placement");
static_assert(outCarAddr(2, 1) == 0x10000 + 2 * 0x4100 + 0x1040, "car address");
// ---- the design doc's channel counts ----
static_assert(channelsTwoCards(5) == 13 && channelsFullRing(5) == 15 && channelsTwoCards(7) == 19 && channelsFullRing(7) == 21, "2x2 counts");
static_assert(edgeHubsEW(2) == 5 && edgeHubsNS(2) == 7 && edgeHubsEW(1) == 10 && edgeHubsNS(1) == 14, "edge hubs");

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static void check_seam(u32 E) {
  u32 N = channelsTwoCards(E);
  for (u32 L = 1; L <= MAX_LINKS; ++L) {
    int seen[64] = {0}; u32 perLink[MAX_LINKS] = {0}, maxch[MAX_LINKS] = {0};
    u32 keys = 0;
    for (u32 e = 0; e < E; ++e) for (u32 k = 0; k < 3; ++k) {
      Slot s = (Slot) k; Assign a;
      bool ex = slotExists(E, e, s), ok = assign(E, L, e, s, a);
      CHECK(ex == ok);
      if (!ok) continue;
      ++keys;
      CHECK(a.link < L);
      u32 ord = ordinalOf(e, s); CHECK(ord < N); CHECK(ord < 64); if (ord < 64) ++seen[ord];
      ++perLink[a.link]; if (a.channel + 1u > maxch[a.link]) maxch[a.link] = a.channel + 1u;
      u32 e2 = 99; Slot s2 = SLOT_CARDINAL;
      CHECK(invert(E, L, a.link, a.channel, e2, s2)); CHECK(e2 == e && s2 == s);
    }
    CHECK(keys == N);
    for (u32 i = 0; i < N; ++i) CHECK(seen[i] == 1);
    u32 mn = 1000, mx = 0;
    for (u32 j = 0; j < L; ++j) { if (perLink[j] < mn) mn = perLink[j]; if (perLink[j] > mx) mx = perLink[j]; CHECK(maxch[j] == perLink[j]); }
    CHECK(mx - mn <= 1);                                       // balanced
    CHECK(mx == maxChannelsPerLink(N, L));
    Assign dummy; u32 ee; Slot ss;
    CHECK(!assign(E, L, E, SLOT_CARDINAL, dummy));             // no such hub
    CHECK(!invert(E, L, 0, N, ee, ss));                        // unused channel
  }
  { Assign z; CHECK(!assign(E, 0, 0, SLOT_CARDINAL, z)); CHECK(!assign(E, MAX_LINKS + 1, 0, SLOT_CARDINAL, z)); }   // L == 0 or > MAX_LINKS rejected
}

int main() {
  struct G { const char* name; u32 cw, ch; } geos[] = { {"2x2 (C2X2_EWP_HUB)", 2, 2}, {"2x1 (C2X1_EWP_HUB)", 2, 1}, {"1x2 (C1X2_EWP_HUB)", 1, 2}, {"1x1 (C1X1_ONE)", 1, 1} };
  printf("NCH_MAX (channels per E1 tile in the images built so far) = %u\n", NCH_MAX);
  printf("%-20s %-4s %3s %6s %5s | channels per link for 4 / 3 / 2 live links (two cards; full ring in brackets) | fits NCH_MAX with 4 links?\n", "cell", "seam", "E", "chans", "ring");
  for (G& g : geos) for (int ns = 0; ns < 2; ++ns) {
    u32 E = ns ? edgeHubsNS(g.cw) : edgeHubsEW(g.ch);
    check_seam(E);
    u32 N = channelsTwoCards(E), R = channelsFullRing(E);
    printf("%-20s %-4s %3u %6u %5u | %2u (%2u) / %2u (%2u) / %2u (%2u) | %s\n", g.name, ns ? "N/S" : "E/W", E, N, R,
           maxChannelsPerLink(N, 4), maxChannelsPerLink(R, 4), maxChannelsPerLink(N, 3), maxChannelsPerLink(R, 3), maxChannelsPerLink(N, 2), maxChannelsPerLink(R, 2),
           fits(R, 4) ? "yes (even full ring)" : (fits(N, 4) ? "yes (two cards), NO for the full ring" : "NO: needs a larger NCH_MAX (and CTL window)"));
  }
  Assign a; CHECK(assign(5, 4, 2, SLOT_DIAG_HI, a)); CHECK(a.link == (ordinalOf(2, SLOT_DIAG_HI) % 4));
  printf("%s\n", fails ? "SOME CHECKS FAILED" : "all ok");
  return fails != 0;
}
