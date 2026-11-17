// ethlinks_test.cpp -- native tests of srcs/host/src/EthLinks.cpp against a MOCK pair of cards (no hardware, no pybind).
// Build/run (from srcs/host/tools):
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -I../include -I../../shared/include ethlinks_test.cpp ../src/EthLinks.cpp ../src/EthSeamHost.cpp -o /tmp/ethlinks_test && /tmp/ethlinks_test
// The mock models just enough of an ETH tile: boot-result words, MACs, soft-reset/PC/END registers, the E1 coming up (ready word + image id) when its
// soft-reset bit is cleared, the E1 acknowledging stop, queue-2 packets copying L1 to the peer with the RX sequence counter, and tt-kmd NOC_CLEANUP.
// It proves THIS LIBRARY'S LOGIC (sequence, allowlist, validation, restore); it says nothing about real E1 behaviour.
#include "EthSeamHost.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
using namespace MFM;
using namespace MFM::EthSeam;

struct Fab;
struct MockPort : EthPort {
  Fab & f; int chip;
  MockPort(Fab & fb, int c) : f(fb), chip(c) { }
  bool rd32(u32 x, u32 a, u32 & v) override ;
  bool wr32(u32 x, u32 a, u32 v) override ;
  bool setNocCleanup(u32 x, u32 addr, u32 data) override ;
};
struct Fab {
  std::map<u64, u32> m; int writes = 0; bool cleanupArmed = false; u32 cleanupAddr = 0, cleanupData = 0, cleanupDataByChip[2] = {0, 0};
  bool imageIdOverride = false; u32 imageId = 0xE1F00016u;
  static u64 key(int c, u32 x, u32 a) { return ((u64) c << 48) | ((u64) x << 40) | a; }
  u32 get(int c, u32 x, u32 a) { auto it = m.find(key(c, x, a)); return it == m.end() ? 0 : it->second; }
  void put(int c, u32 x, u32 a, u32 v) { m[key(c, x, a)] = v; }
  void init(u32 x) {
    for (int c = 0; c < 2; ++c) {
      put(c, x, 0xFFB20048u, 0x00020000u | 10u); put(c, x, 0x7CC04, 1);
      u32 r = 0x7CC00 + 4 * 240;
      put(c, x, r + 12, 0x208c47); put(c, x, r + 16, 0x100000u * (c + 1) + x); put(c, x, r + 28, 1);
      r = 0x7CC00 + 4 * 248;
      put(c, x, r + 12, 0x208c47); put(c, x, r + 16, 0x100000u * ((c ^ 1) + 1) + x); put(c, x, r + 28, 1);
      put(c, x, 0xFFB121B0u, 0x47000u); put(c, x, 0xFFB14008u, 0x4000); put(c, x, 0xFFB1400Cu, 0x7fff);
      put(c, x, 0xFFB92000u + 0x0c, 0x5dc);
    }
  }
};
bool MockPort::rd32(u32 x, u32 a, u32 & v) { v = f.get(chip, x, a); return true; }
bool MockPort::wr32(u32 x, u32 a, u32 v) {
  f.put(chip, x, a, v); ++f.writes;
  if (a == 0xFFB121B0u && !(v & 0x1000u)) { f.put(chip, x, CTL_BASE + CTL_READY, E1_READY_WORD); f.put(chip, x, CTL_BASE + CTL_IMAGE_ID, f.imageId); }   // fake E1 comes up
  if (a == CTL_BASE + CTL_STOP && v == 1u) f.put(chip, x, CTL_BASE + CTL_DONE, 1);
  if (a == 0xFFB92000u + 4 && v == 2u) {                                                                                                        // TXQ2 CMD: copy to the peer
    u32 src = f.get(chip, x, 0xFFB92000u + 0x14), n = f.get(chip, x, 0xFFB92000u + 0x18), dst = f.get(chip, x, 0xFFB92000u + 0x1c);
    int p = chip ^ 1;
    bool ok = (f.get(chip, x, 0xFFB92000u) & 1) && (f.get(p, x, 0xFFB96000u) & 2) && ((f.get(chip, x, 0xFFB92000u + 0x80) >> 8) & 15) == 1;
    if (ok) {
      for (u32 i = 0; i < n; i += 4) f.put(p, x, dst + i, f.get(chip, x, src + i));
      f.put(p, x, 0xFFB96000u + 0x40, (f.get(p, x, 0xFFB96000u + 0x40) + 1) & 0xff);
    }
  }
  return true;
}
bool MockPort::setNocCleanup(u32, u32 addr, u32 data) { f.cleanupArmed = true; f.cleanupAddr = addr; f.cleanupData = data; f.cleanupDataByChip[chip] = data; return true; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static std::vector<u32> fakeImage() { std::vector<u32> v(100); for (size_t i = 0; i < v.size(); ++i) v[i] = 0x13 + (u32) i * 0x100; return v; }
static EthChannelSetup setup(u32 t6x, u32 t6y) {
  EthChannelSetup s;
  s.channels.push_back({0, LEG_OUT | LEG_IN_NOC, (u8) t6x, (u8) t6y, 0x100000u});
  s.channels.push_back({3, LEG_OUT | LEG_IN_NOC, (u8) t6x, (u8) t6y, 0x100000u + RING_BYTES});
  s.ringCars = 4;
  return s;
}

int main() {
  std::vector<std::string> logs;
  ethLinksSetLogger([&](const std::string & s) { logs.push_back(s); });
  const u32 X = 6;

  // ---- 1. happy path, then teardown restores everything ----
  {
    Fab f; f.init(X); MockPort pa(f, 0), pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    bool up = L.bringUp(fakeImage(), setup(1, 2), setup(2, 3));
    CHECK(up); if (!up) printf("  error: %s\n", L.lastError().c_str());
    CHECK(L.isUp());
    for (int c = 0; c < 2; ++c) {
      CHECK(f.get(c, X, 0xFFB121B0u) == (0x47000u & ~0x1000u));                        // E1 released
      CHECK(f.get(c, X, 0xFFB14008u) == E1_CODE && f.get(c, X, 0xFFB1400Cu) == E1_CODE + 400u);
      CHECK(f.get(c, X, E1_CODE) == 0x13 && f.get(c, X, E1_CODE + 4 * 99) == 0x13 + 99 * 0x100);
      CHECK(f.get(c, X, CTL_BASE + CTL_GO) == 1 && f.get(c, X, CTL_BASE + CTL_NCH) == 4 && f.get(c, X, CTL_BASE + CTL_MODE) == E1_MODE_ROUTER);
      CHECK(f.get(c, X, CTL_BASE + CTL_FLAGS) == E1F_CSR && f.get(c, X, CTL_BASE + CTL_RING_CARS) == 4);
      CHECK(f.get(c, X, CTL_BASE + CTL_NODE_ID) == ((1u << 6) | X));
      CHECK(f.get(c, X, 0xFFB96000u) == 2 && f.get(c, X, 0xFFB92000u) == 1 && f.get(c, X, 0xFFB92000u + 0x80) == (1u << 8));
      u32 t6x = c == 0 ? 1 : 2, t6y = c == 0 ? 2 : 3;
      for (u32 ch : {0u, 3u}) {
        u32 cb = channelBlockAddr(ch);
        CHECK(f.get(c, X, cb + 4 * CW_OUT) == outRingAddr(ch) && f.get(c, X, cb + 4 * CW_IN) == inRingAddr(ch) && f.get(c, X, cb + 4 * CW_PEERIN) == inRingAddr(ch));
        CHECK(f.get(c, X, cb + 4 * CW_T6NODE) == ((t6y << 6) | t6x) && f.get(c, X, cb + 4 * CW_LEGS) == (LEG_OUT | LEG_IN_NOC));
        CHECK(f.get(c, X, cb + 4 * CW_T6BASE) == 0x100000u + (ch == 3 ? RING_BYTES : 0));
        for (u32 i = 0; i < RING_CARS; ++i) CHECK(f.get(c, X, outRingAddr(ch) + i * CAR_BYTES) == 0 && f.get(c, X, inRingAddr(ch) + i * CAR_BYTES) == 0);
      }
      CHECK(f.get(c, X, channelBlockAddr(1) + 4 * CW_LEGS) == 0);                       // unused channel: no legs
      CHECK(f.get(c, X, CTL_BASE + CTL_IMAGE_ID) == 0xE1F00016u);                       // image id survived the config zeroing
    }
    CHECK(f.cleanupArmed && f.cleanupAddr == 0xFFB121B0u && f.cleanupData == 0x47000u);
    CHECK(f.get(0, X, 0x7CC04) == 1);
    L.tearDown();
    for (int c = 0; c < 2; ++c) {
      CHECK(f.get(c, X, 0xFFB121B0u) == 0x47000u && f.get(c, X, 0xFFB14008u) == 0x4000 && f.get(c, X, 0xFFB1400Cu) == 0x7fff);
      CHECK(f.get(c, X, 0xFFB92000u) == 0 && f.get(c, X, 0xFFB96000u) == 0 && f.get(c, X, 0xFFB92000u + 0x80) == 0 && f.get(c, X, 0xFFB92000u + 0x84) == 0);
      for (u32 o : {0x10u, 0x14u, 0x18u, 0x1cu}) CHECK(f.get(c, X, 0xFFB98280u + o) == 0);
      CHECK(f.get(c, X, outRingAddr(0)) == 0 && f.get(c, X, inRingAddr(0)) == 0);        // smoke packet cleaned up
    }
    CHECK(!L.isUp());
    int w = f.writes; L.tearDown(); CHECK(f.writes == w);                                // second teardown is a no-op
  }

  // ---- 2. refusals leave the hardware untouched ----
  struct Case { const char * name; void (*prep)(Fab&, u32); bool force; };
  Case cases[] = {
    {"queue 2 already in use",  [](Fab & f, u32 x) { f.put(0, x, 0xFFB96000u, 2); }, false},
    {"E1 not in reset",         [](Fab & f, u32 x) { f.put(1, x, 0xFFB121B0u, 0x46000u); }, false},
    {"link down",               [](Fab & f, u32) { f.put(0, X, 0x7CC04, 0); }, false},
    {"harvested",               [](Fab & f, u32 x) { f.put(1, x, 0xFFB20100u, 1u << 12); }, false},
    {"not an ETH tile",         [](Fab & f, u32 x) { f.put(0, x, 0xFFB20048u, 0x00010000u); }, false},
    {"peers do not match",      [](Fab & f, u32 x) { f.put(0, x, 0x7CC00 + 4 * 248 + 16, 0xdeadu); }, false},
    {"wrong image id",          [](Fab & f, u32) { f.imageId = 0x12345678u; }, false},
  };
  for (Case & cs : cases) {
    Fab f; f.init(X); MockPort pa(f, 0), pb(f, 1);
    cs.prep(f, X);
    EthLink L(pa, 0, pb, 1, X);
    bool up = L.bringUp(fakeImage(), setup(1, 2), setup(2, 3), cs.force);
    CHECK(!up); CHECK(!L.lastError().empty() || std::string(cs.name) == "peers do not match");
    // everything an aborted bring-up may have touched is back to its starting state
    for (int c = 0; c < 2; ++c) {
      u32 sr = f.get(c, X, 0xFFB121B0u);
      CHECK(sr == 0x47000u || (c == 1 && std::string(cs.name) == "E1 not in reset" && sr == 0x46000u));
      CHECK(f.get(c, X, 0xFFB14008u) == 0x4000 && f.get(c, X, 0xFFB1400Cu) == 0x7fff);
      if (std::string(cs.name) != "queue 2 already in use") CHECK(f.get(c, X, 0xFFB92000u) == 0 && f.get(c, X, 0xFFB96000u) == 0);
    }
    if (std::string(cs.name) == "queue 2 already in use" || std::string(cs.name) == "harvested" || std::string(cs.name) == "link down" ||
        std::string(cs.name) == "not an ETH tile" || std::string(cs.name) == "peers do not match") CHECK(f.writes == 0);   // refused before any write
    printf("  refusal '%s': %s\n", cs.name, L.lastError().c_str());
  }

  // ---- 3. force overrides a busy E1 / used queue (and only then), and the original soft-reset value is what teardown restores ----
  {
    Fab f; f.init(X); f.put(1, X, 0xFFB121B0u, 0x46000u); MockPort pa(f, 0), pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    CHECK(L.bringUp(fakeImage(), setup(1, 2), setup(2, 3), /*force*/ true));
    CHECK(f.get(1, X, 0xFFB121B0u) == (0x46000u & ~0x1000u) && f.cleanupDataByChip[1] == 0x46000u && f.cleanupDataByChip[0] == 0x47000u);
    L.tearDown();
    CHECK(f.get(1, X, 0xFFB121B0u) == 0x46000u);
  }

  // ---- 4. channel setup validation ----
  {
    Fab f; f.init(X); MockPort pa(f, 0), pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    auto bad = [&](EthChannelSetup s, const char * what) {
      Fab g; g.init(X); MockPort qa(g, 0), qb(g, 1);
      EthLink LL(qa, 0, qb, 1, X);
      bool up = LL.bringUp(fakeImage(), s, setup(2, 3));
      CHECK(!up);
      printf("  bad setup (%s): %s\n", what, LL.lastError().c_str());
      CHECK(g.get(0, X, 0xFFB121B0u) == 0x47000u && g.get(1, X, 0xFFB121B0u) == 0x47000u);   // both back in reset
    };
    EthChannelSetup s = setup(8, 3);   bad(s, "T6 column 8 is not Tensix");
    s = setup(1, 1);                   bad(s, "T6 row 1 is not Tensix");
    s = setup(1, 2); s.channels[0].t6Base = 0x100008; bad(s, "T6 base not 16-aligned");
    s = setup(1, 2); s.channels[0].t6Base = 0x17F000; bad(s, "T6 ring would run past L1");
    s = setup(1, 2); s.channels[1].channel = 0; bad(s, "duplicate channel");
    s = setup(1, 2); s.channels[1].channel = (u8) NCH_MAX; bad(s, "channel >= NCH_MAX");
    s = setup(1, 2); s.ringCars = 5; bad(s, "ring depth 5");
    s = setup(1, 2); s.channels.clear(); bad(s, "no channels");
  }

  // ---- 5. image file loader ----
  {
    std::vector<u32> w; std::string err;
    CHECK(!ethLoadImageFile("/nonexistent/e1.bin", w, err) && !err.empty());
    FILE * f = fopen("/tmp/ethlinks_test_odd.bin", "wb"); fwrite("abcde", 1, 5, f); fclose(f);
    CHECK(!ethLoadImageFile("/tmp/ethlinks_test_odd.bin", w, err));
    f = fopen("/tmp/ethlinks_test_ok.bin", "wb"); unsigned char b[8] = {1, 0, 0, 0, 0x78, 0x56, 0x34, 0x12}; fwrite(b, 1, 8, f); fclose(f);
    CHECK(ethLoadImageFile("/tmp/ethlinks_test_ok.bin", w, err) && w.size() == 2 && w[0] == 1 && w[1] == 0x12345678u);
  }


  // ---- 6. EthSeamHost: registry, error paths, chipClosing ----
  {
    Fab f; f.init(X);
    FILE * im = fopen("/tmp/ethlinks_test_image.bin", "wb"); std::vector<u32> img = fakeImage(); fwrite(img.data(), 4, img.size(), im); fclose(im);
    EthSeamHost & h = EthSeamHost::get();
    CHECK(h.status() == "no eth-seam links");
    h.setPortFactory(nullptr);
    CHECK(h.bringUpLink(0, 1, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true).find("factory") != std::string::npos);
    h.setPortFactory([&](u32 chip, std::string & err) -> std::unique_ptr<EthPort> {
      if (chip > 1) { err = "no such chip"; return nullptr; }
      return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
    });
    CHECK(h.bringUpLink(0, 0, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true) == "a link needs two different chips");
    CHECK(h.bringUpLink(0, 2, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true) == "no such chip");
    CHECK(!h.bringUpLink(0, 1, X, "/nonexistent.bin", setup(1, 2), setup(2, 3), false, true).empty());
    CHECK(f.writes == 0);                                                  // image / chip errors before any write
    std::string err = h.bringUpLink(0, 1, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true);
    CHECK(err.empty()); if (!err.empty()) printf("  error: %s\n", err.c_str());
    CHECK(h.status().find("UP") != std::string::npos);
    CHECK(h.bringUpLink(0, 1, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true).find("already") != std::string::npos);
    EthHealth eh; std::string e2; u32 v = 0;
    CHECK(h.health(0, X, eh, e2) && eh.port == 1);
    CHECK(!h.health(0, X + 1, eh, e2));
    f.put(1, X, channelBlockAddr(3) + 4 * CW_S_FWD, 1234);
    CHECK(h.channelStat(1, X, 3, CW_S_FWD, v, e2) && v == 1234);
    CHECK(!h.channelStat(1, X, NCH_MAX, 0, v, e2) && !h.channelStat(1, X, 0, 24, v, e2));
    CHECK(h.tearDownLink(1, 0, X) == "" && f.get(0, X, 0xFFB121B0u) == 0x47000u && f.get(1, X, 0xFFB121B0u) == 0x47000u);
    CHECK(h.tearDownLink(1, 0, X) == "no such link");
    CHECK(h.bringUpLink(0, 1, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true).empty());   // can come up again after teardown
    h.chipClosing(1);                                                      // chip going away: link down, state restored, port dropped
    CHECK(f.get(0, X, 0xFFB121B0u) == 0x47000u && f.get(1, X, 0xFFB121B0u) == 0x47000u && f.get(0, X, 0xFFB92000u) == 0 && h.status() == "no eth-seam links");
    CHECK(h.bringUpLink(0, 1, X, "/tmp/ethlinks_test_image.bin", setup(1, 2), setup(2, 3), false, true).empty());   // a fresh port is made on demand
    h.tearDownAll();
    CHECK(h.status() == "no eth-seam links" && f.get(1, X, 0xFFB121B0u) == 0x47000u);
  }

  {
    // seam declaration: both directions, validation
    EthSeamHost & h = EthSeamHost::get();
    h.clearSeams();
    CHECK(h.declareSeam(0, DIR4_E, 1, { 6, 7, 10, 11 }).empty());
    EthSeam::SeamTable t0 = h.seamTableFor(0);
    EthSeam::SeamTable t1 = h.seamTableFor(1);
    CHECK(t0.n[DIR4_E] == 4 && t1.n[DIR4_W] == 4);
    CHECK(t0.ethX[DIR4_E][2] == 10 && t1.ethX[DIR4_W][2] == 10);
    CHECK(t0.n[DIR4_W] == 0 && t1.n[DIR4_E] == 0);
    CHECK(h.seamTableFor(7).n[DIR4_E] == 0);                 // unknown chip
    CHECK(!h.declareSeam(0, DIR4_E, 2, { 6 }).empty());      // already used
    CHECK(!h.declareSeam(0, DIR4_S, 0, { 6 }).empty());      // same chip
    CHECK(!h.declareSeam(0, DIR4_S, 2, { }).empty());        // no links
    CHECK(!h.declareSeam(0, DIR4_S, 2, { 1, 2, 3, 4, 5 }).empty());
    CHECK(!h.declareSeam(0, 4, 2, { 6 }).empty());           // bad dir
    CHECK(h.declareSeam(0, DIR4_S, 2, { 3, 4 }).empty());
    CHECK(h.seamTableFor(2).n[DIR4_N] == 2);
    h.clearSeams();
    CHECK(h.seamTableFor(0).n[DIR4_E] == 0);
  }

  printf("%zu log lines; %s\n", logs.size(), fails ? "SOME CHECKS FAILED" : "all ok");
  return fails != 0;
}
