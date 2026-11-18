// ethlinks_test.cpp -- native tests of srcs/host/src/EthLinks.cpp and
// EthSeamHost.cpp against a MOCK pair of cards (no hardware, no pybind).
//
// Build/run: srcs/host/tools/run_native_tests.sh ethlinks
//
// The mock models just enough of an ETH tile: boot-result words, MACs,
// soft-reset/PC/END registers, the E1 coming up (ready word + image id)
// when its soft-reset bit is cleared, the E1 acknowledging stop, queue-2
// packets copying L1 to the peer with the RX sequence counter, and tt-kmd
// NOC_CLEANUP.  It proves THIS LIBRARY'S LOGIC (sequence, allowlist,
// validation, restore, planning); it says nothing about real E1 behaviour.
#include "EthSeamHost.h"
#include "UxC.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace MFM;
using namespace MFM::EthSeam;

namespace {
  // ETH tile registers the mock understands (see EthLinks.cpp)
  constexpr u32 BOOT_RESULTS = 0x7CC00u;
  constexpr u32 NIU0_ENDPOINT = 0xFFB20048u;
  constexpr u32 NIU0_CFG = 0xFFB20100u;
  constexpr u32 SOFT_RESET = 0xFFB121B0u;
  constexpr u32 E1_PC = 0xFFB14008u;
  constexpr u32 E1_END = 0xFFB1400Cu;
  constexpr u32 TXQ2 = 0xFFB92000u;
  constexpr u32 RXQ2 = 0xFFB96000u;
  constexpr u32 TXHDR1 = 0xFFB98280u;

  constexpr u32 SR_E1 = 0x1000u;
  constexpr u32 SR_DEFAULT = 0x47000u;
  constexpr u32 X = 6u;                 //< the ETH tile most tests use
}

struct Fab;

struct MockPort : EthPort {
  Fab & f;
  int chip;
  MockPort(Fab & fb, int c) : f(fb), chip(c) { }
  bool rd32(u32 x, u32 a, u32 & v) override ;
  bool wr32(u32 x, u32 a, u32 v) override ;
  bool setNocCleanup(u32 x, u32 addr, u32 data) override ;
};

/** Both cards' tile memories, keyed by (chip, ETH x, address). */
struct Fab {
  std::map<u64, u32> m;
  std::map<u32, int> peerXorOfTile;     //< ETH x -> cable partner xor
  int writes = 0;
  bool cleanupArmed = false;
  u32 cleanupAddr = 0;
  u32 cleanupData = 0;
  u32 cleanupDataByChip[4] = { 0, 0, 0, 0 };
  u32 imageId = 0xE1F00016u;            //< what a fake E1 reports
  // (chip, x) -> number of port-status reads that still say 2 (training)
  std::map<u64, int> slowUp;
  // ETH x whose port drops to 2 as soon as its E1 is released (a link that
  // fails after it was brought up); 0 = none
  u32 dropPortAtE1 = 0;

  static u64 key(int c, u32 x, u32 a) {
    return ((u64) c << 48) | ((u64) x << 40) | a;
  }

  u32 get(int c, u32 x, u32 a) {
    auto it = m.find(key(c, x, a));
    return it == m.end() ? 0 : it->second;
  }

  void put(int c, u32 x, u32 a, u32 v) {
    m[key(c, x, a)] = v;
  }

  /** Make tile x of the chips a healthy, linked-up, pristine ETH tile;
      chip c's cable partner is chip (c ^ peerXor). */
  void init(u32 x, int peerXor = 1, int chips = 2) {
    peerXorOfTile[x] = peerXor;
    for (int c = 0; c < chips; ++c) {
      put(c, x, NIU0_ENDPOINT, 0x00020000u | 10u);
      put(c, x, BOOT_RESULTS + 4, 1);               // port status Up

      u32 own = BOOT_RESULTS + 4 * 240;             // own chip-info record
      put(c, x, own + 12, 0x208c47);
      put(c, x, own + 16, 0x100000u * (c + 1) + x);
      put(c, x, own + 28, 1);

      u32 peer = BOOT_RESULTS + 4 * 248;            // peer chip-info record
      put(c, x, peer + 12, 0x208c47);
      put(c, x, peer + 16, 0x100000u * ((c ^ peerXor) + 1) + x);
      put(c, x, peer + 28, 1);

      put(c, x, SOFT_RESET, SR_DEFAULT);
      put(c, x, E1_PC, 0x4000);
      put(c, x, E1_END, 0x7fff);
      put(c, x, TXQ2 + 0x0c, 0x5dc);
    }
  }
};

bool MockPort::rd32(u32 x, u32 a, u32 & v) {
  v = f.get(chip, x, a);
  if (a == BOOT_RESULTS + 4) {
    auto it = f.slowUp.find(Fab::key(chip, x, 0));
    if (it != f.slowUp.end() && it->second > 0) {
      --it->second;
      v = 2;
    }
  }
  return true;
}

bool MockPort::wr32(u32 x, u32 a, u32 v) {
  f.put(chip, x, a, v);
  ++f.writes;

  // releasing the E1's soft reset: the fake E1 comes up
  if (a == SOFT_RESET && !(v & SR_E1)) {
    if (f.dropPortAtE1 == x) f.put(chip, x, BOOT_RESULTS + 4, 2);
    f.put(chip, x, CTL_BASE + CTL_READY, E1_READY_WORD);
    f.put(chip, x, CTL_BASE + CTL_IMAGE_ID, f.imageId);
  }

  // a stop request: the fake E1 acknowledges
  if (a == CTL_BASE + CTL_STOP && v == 1u) {
    f.put(chip, x, CTL_BASE + CTL_DONE, 1);
  }

  // TXQ2 CMD: copy source L1 to the peer's destination, bump its RX seq
  if (a == TXQ2 + 4 && v == 2u) {
    u32 src = f.get(chip, x, TXQ2 + 0x14);
    u32 n = f.get(chip, x, TXQ2 + 0x18);
    u32 dst = f.get(chip, x, TXQ2 + 0x1c);
    auto px = f.peerXorOfTile.find(x);
    int p = chip ^ (px == f.peerXorOfTile.end() ? 1 : px->second);
    bool txOn = f.get(chip, x, TXQ2) & 1;
    bool rxRaw = f.get(p, x, RXQ2) & 2;
    bool unicast = ((f.get(chip, x, TXQ2 + 0x80) >> 8) & 15) == 1;
    if (txOn && rxRaw && unicast) {
      for (u32 i = 0; i < n; i += 4) {
        f.put(p, x, dst + i, f.get(chip, x, src + i));
      }
      u32 seq = f.get(p, x, RXQ2 + 0x40);
      f.put(p, x, RXQ2 + 0x40, (seq + 1) & 0xff);
    }
  }
  return true;
}

bool MockPort::setNocCleanup(u32, u32 addr, u32 data) {
  f.cleanupArmed = true;
  f.cleanupAddr = addr;
  f.cleanupData = data;
  f.cleanupDataByChip[chip] = data;
  return true;
}

static int fails = 0;
static std::vector<std::string> logs;

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      printf("FAIL line %d: %s\n", __LINE__, #c);         \
      ++fails;                                            \
    }                                                     \
  } while (0)

static std::vector<u32> fakeImage() {
  std::vector<u32> v(100);
  for (size_t i = 0; i < v.size(); ++i) {
    v[i] = 0x13 + (u32) i * 0x100;
  }
  return v;
}

static EthChannelSetup setup(u32 t6x, u32 t6y) {
  EthChannelSetup s;
  s.channels.push_back({ 0,
                         LEG_OUT | LEG_IN_NOC,
                         (u8) t6x,
                         (u8) t6y,
                         0x100000u });
  s.channels.push_back({ 3,
                         LEG_OUT | LEG_IN_NOC,
                         (u8) t6x,
                         (u8) t6y,
                         0x100000u + RING_BYTES });
  s.ringCars = 4;
  return s;
}

// Hub lookup for the planner tests: real CT6 -> NoC0 mapping, and a
// fake but valid InterHub base per hub.
static bool mockHub(u32 chip,
                    u32 x,
                    u32 y,
                    EthSeamHost::SeamHub & hub,
                    std::string & err) {
  if (chip > 3) {
    err = "no such chip";
    return false;
  }
  U8C ct6((u8) x, (u8) y);
  U8C noc0 = U8C::makeNoC0CoordFromCT6Coord(ct6);
  hub.noc0x = noc0.x;
  hub.noc0y = noc0.y;
  hub.ihubBase = 0x40000u + 0x10u * (x + 16u * y) + 0x1000u * chip;
  hub.chubBase = hub.ihubBase + 0x8000u;
  return true;
}

static const char * IMAGE = "/tmp/ethlinks_test_image.bin";

// ---- 1. happy path, then teardown restores everything ----

static void test_happy_path() {
  Fab f;
  f.init(X);
  MockPort pa(f, 0);
  MockPort pb(f, 1);
  EthLink L(pa, 0, pb, 1, X);
  bool up = L.bringUp(fakeImage(), setup(1, 2), setup(2, 3));
  CHECK(up);
  if (!up) printf("  error: %s\n", L.lastError().c_str());
  CHECK(L.isUp());

  for (int c = 0; c < 2; ++c) {
    auto g = [&](u32 a) { return f.get(c, X, a); };
    CHECK(g(SOFT_RESET) == (SR_DEFAULT & ~SR_E1));      // E1 released
    CHECK(g(E1_PC) == E1_CODE);
    CHECK(g(E1_END) == E1_CODE + 400u);
    CHECK(g(E1_CODE) == 0x13);
    CHECK(g(E1_CODE + 4 * 99) == 0x13 + 99 * 0x100);
    CHECK(g(CTL_BASE + CTL_GO) == 1);
    CHECK(g(CTL_BASE + CTL_NCH) == 4);
    CHECK(g(CTL_BASE + CTL_MODE) == E1_MODE_ROUTER);
    CHECK(g(CTL_BASE + CTL_FLAGS) == E1F_CSR);
    CHECK(g(CTL_BASE + CTL_RING_CARS) == 4);
    CHECK(g(CTL_BASE + CTL_NODE_ID) == ((1u << 6) | X));
    CHECK(g(RXQ2) == 2);
    CHECK(g(TXQ2) == 1);
    CHECK(g(TXQ2 + 0x80) == (1u << 8));

    u32 t6x = c == 0 ? 1 : 2;
    u32 t6y = c == 0 ? 2 : 3;
    for (u32 ch : { 0u, 3u }) {
      u32 cb = channelBlockAddr(ch);
      CHECK(g(cb + 4 * CW_OUT) == outRingAddr(ch));
      CHECK(g(cb + 4 * CW_IN) == inRingAddr(ch));
      CHECK(g(cb + 4 * CW_PEERIN) == inRingAddr(ch));
      CHECK(g(cb + 4 * CW_T6NODE) == ((t6y << 6) | t6x));
      CHECK(g(cb + 4 * CW_LEGS) == (LEG_OUT | LEG_IN_NOC));
      CHECK(g(cb + 4 * CW_T6BASE) == 0x100000u + (ch == 3 ? RING_BYTES : 0));
      for (u32 i = 0; i < RING_CARS; ++i) {
        CHECK(g(outRingAddr(ch) + i * CAR_BYTES) == 0);
        CHECK(g(inRingAddr(ch) + i * CAR_BYTES) == 0);
      }
    }
    // unused channel: no legs
    CHECK(g(channelBlockAddr(1) + 4 * CW_LEGS) == 0);
    // image id survived the config zeroing
    CHECK(g(CTL_BASE + CTL_IMAGE_ID) == 0xE1F00016u);
  }
  CHECK(f.cleanupArmed);
  CHECK(f.cleanupAddr == SOFT_RESET);
  CHECK(f.cleanupData == SR_DEFAULT);
  CHECK(f.get(0, X, BOOT_RESULTS + 4) == 1);

  L.tearDown();
  for (int c = 0; c < 2; ++c) {
    auto g = [&](u32 a) { return f.get(c, X, a); };
    CHECK(g(SOFT_RESET) == SR_DEFAULT);
    CHECK(g(E1_PC) == 0x4000);
    CHECK(g(E1_END) == 0x7fff);
    CHECK(g(TXQ2) == 0);
    CHECK(g(RXQ2) == 0);
    CHECK(g(TXQ2 + 0x80) == 0);
    CHECK(g(TXQ2 + 0x84) == 0);
    for (u32 o : { 0x10u, 0x14u, 0x18u, 0x1cu }) {
      CHECK(g(TXHDR1 + o) == 0);
    }
    // smoke packet cleaned up
    CHECK(g(outRingAddr(0)) == 0);
    CHECK(g(inRingAddr(0)) == 0);
  }
  CHECK(!L.isUp());

  // a second teardown is a no-op
  int w = f.writes;
  L.tearDown();
  CHECK(f.writes == w);
}

// ---- 2. refusals leave the hardware untouched ----

struct RefusalCase {
  const char * name;
  void (*prep)(Fab &);
  bool queue2PreUsed;       //< queue 2 was already in use, so stays set
  bool refusedBeforeWrite;  //< validation refuses before any write at all
  u32 chip1ResetAfter;      //< chip 1's soft-reset register afterwards
  bool errorMayBeEmpty;
};

// ---- 1b. links that are slow to train, stuck, or drop later ----

static void test_up_wait() {
  const u32 saved = ethLinksUpWaitMs();

  // slow to train: Up after a few polls, within the wait
  ethLinksSetUpWaitMs(3000);
  {
    Fab f;
    f.init(X);
    f.slowUp[Fab::key(0, X, 0)] = 5;
    f.slowUp[Fab::key(1, X, 0)] = 3;
    MockPort pa(f, 0);
    MockPort pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    CHECK(L.bringUp(fakeImage(), setup(1, 2), setup(2, 3)));
    CHECK(L.isUp());
    L.tearDown();
  }

  // stuck at 2: fails after the wait, and the error says what BOTH ends saw
  ethLinksSetUpWaitMs(300);
  {
    Fab f;
    f.init(X);
    f.put(0, X, BOOT_RESULTS + 4, 2);
    f.put(1, X, BOOT_RESULTS + 4, 2);
    MockPort pa(f, 0);
    MockPort pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    CHECK(!L.bringUp(fakeImage(), setup(1, 2), setup(2, 3)));
    const std::string & e = L.lastError();
    CHECK(e.find("link not Up (port status 2) after waiting") !=
          std::string::npos);
    CHECK(e.find("peer: ") != std::string::npos);
    CHECK(e.find("heartbeat") != std::string::npos);
    CHECK(!L.isUp());
  }

  // only the second end is down: the error names the first and the peer
  {
    Fab f;
    f.init(X);
    f.put(0, X, BOOT_RESULTS + 4, 2);
    MockPort pa(f, 0);
    MockPort pb(f, 1);
    EthLink L(pa, 0, pb, 1, X);
    CHECK(!L.bringUp(fakeImage(), setup(1, 2), setup(2, 3)));
    CHECK(L.lastError().find("peer bh1:eth") != std::string::npos);
  }
  ethLinksSetUpWaitMs(saved);
}

// A link that comes up and then drops while the others come up fails the
// whole bring-up at the final check, with everything torn down.
static void test_verify_after_bringup() {
  Fab f;
  for (u32 x : { 6u, 7u, 10u, 11u }) f.init(x, 1, 2);
  EthSeamHost & h = EthSeamHost::get();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.clearSeams();
  h.setPortFactory([&f](u32 chip, std::string &)
                   -> std::unique_ptr<EthPort> {
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  h.setHubLookup(mockHub);
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());
  f.dropPortAtE1 = 7;
  std::string err = h.bringUpSeams(IMAGE, 2, 4, false, true);
  CHECK(err.find("no longer Up") != std::string::npos);
  CHECK(h.status() == "no eth-seam links");
  // healthy again: succeeds
  f.dropPortAtE1 = 0;
  for (u32 x : { 6u, 7u, 10u, 11u }) f.init(x, 1, 2);
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).empty());
  h.tearDownAll();
  h.clearSeams();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.setPortFactory(nullptr);
}

static void test_refusals() {
  const RefusalCase cases[] = {
    { "queue 2 already in use",
      [](Fab & f) { f.put(0, X, RXQ2, 2); },
      true, true, SR_DEFAULT, false },
    { "E1 not in reset",
      [](Fab & f) { f.put(1, X, SOFT_RESET, 0x46000u); },
      false, false, 0x46000u, false },
    { "link down",
      [](Fab & f) { f.put(0, X, BOOT_RESULTS + 4, 0); },
      false, true, SR_DEFAULT, false },
    { "harvested",
      [](Fab & f) { f.put(1, X, NIU0_CFG, 1u << 12); },
      false, true, SR_DEFAULT, false },
    { "not an ETH tile",
      [](Fab & f) { f.put(0, X, NIU0_ENDPOINT, 0x00010000u); },
      false, true, SR_DEFAULT, false },
    { "peers do not match",
      [](Fab & f) { f.put(0, X, BOOT_RESULTS + 4 * 248 + 16, 0xdeadu); },
      false, true, SR_DEFAULT, true },
    { "wrong image id",
      [](Fab & f) { f.imageId = 0x12345678u; },
      false, false, SR_DEFAULT, false },
  };

  for (const RefusalCase & cs : cases) {
    Fab f;
    f.init(X);
    MockPort pa(f, 0);
    MockPort pb(f, 1);
    cs.prep(f);
    EthLink L(pa, 0, pb, 1, X);
    bool up = L.bringUp(fakeImage(), setup(1, 2), setup(2, 3), false);
    CHECK(!up);
    CHECK(!L.lastError().empty() || cs.errorMayBeEmpty);

    // everything an aborted bring-up may have touched is back as it was
    for (int c = 0; c < 2; ++c) {
      u32 want = (c == 1) ? cs.chip1ResetAfter : SR_DEFAULT;
      CHECK(f.get(c, X, SOFT_RESET) == want);
      CHECK(f.get(c, X, E1_PC) == 0x4000);
      CHECK(f.get(c, X, E1_END) == 0x7fff);
      if (!cs.queue2PreUsed) {
        CHECK(f.get(c, X, TXQ2) == 0);
        CHECK(f.get(c, X, RXQ2) == 0);
      }
    }
    if (cs.refusedBeforeWrite) CHECK(f.writes == 0);
    printf("  refusal '%s': %s\n", cs.name, L.lastError().c_str());
  }
}

// ---- 3. force overrides a busy E1 / used queue (and only then), and
//         the original soft-reset value is what teardown restores ----

static void test_force() {
  Fab f;
  f.init(X);
  f.put(1, X, SOFT_RESET, 0x46000u);
  MockPort pa(f, 0);
  MockPort pb(f, 1);
  EthLink L(pa, 0, pb, 1, X);
  CHECK(L.bringUp(fakeImage(), setup(1, 2), setup(2, 3), /*force*/ true));
  CHECK(f.get(1, X, SOFT_RESET) == (0x46000u & ~SR_E1));
  CHECK(f.cleanupDataByChip[1] == 0x46000u);
  CHECK(f.cleanupDataByChip[0] == SR_DEFAULT);
  L.tearDown();
  CHECK(f.get(1, X, SOFT_RESET) == 0x46000u);
}

// ---- 4. channel setup validation ----

static void expectBadSetup(const EthChannelSetup & s, const char * what) {
  Fab g;
  g.init(X);
  MockPort qa(g, 0);
  MockPort qb(g, 1);
  EthLink LL(qa, 0, qb, 1, X);
  bool up = LL.bringUp(fakeImage(), s, setup(2, 3));
  CHECK(!up);
  printf("  bad setup (%s): %s\n", what, LL.lastError().c_str());
  // both ends back in reset
  CHECK(g.get(0, X, SOFT_RESET) == SR_DEFAULT);
  CHECK(g.get(1, X, SOFT_RESET) == SR_DEFAULT);
}

static void test_channel_validation() {
  EthChannelSetup s = setup(8, 3);
  expectBadSetup(s, "T6 column 8 is not Tensix");

  s = setup(1, 1);
  expectBadSetup(s, "T6 row 1 is not Tensix");

  s = setup(1, 2);
  s.channels[0].t6Base = 0x100008;
  expectBadSetup(s, "T6 base not 16-aligned");

  s = setup(1, 2);
  s.channels[0].t6Base = 0x17F000;
  expectBadSetup(s, "T6 ring would run past L1");

  s = setup(1, 2);
  s.channels[1].channel = 0;
  expectBadSetup(s, "duplicate channel");

  s = setup(1, 2);
  s.channels[1].channel = (u8) NCH_MAX;
  expectBadSetup(s, "channel >= NCH_MAX");

  s = setup(1, 2);
  s.ringCars = 5;
  expectBadSetup(s, "ring depth 5");

  s = setup(1, 2);
  s.channels.clear();
  expectBadSetup(s, "no channels");
}

// ---- 5. image file loader ----

static void test_image_loader() {
  std::vector<u32> w;
  std::string err;
  CHECK(!ethLoadImageFile("/nonexistent/e1.bin", w, err));
  CHECK(!err.empty());

  FILE * f = fopen("/tmp/ethlinks_test_odd.bin", "wb");
  fwrite("abcde", 1, 5, f);
  fclose(f);
  CHECK(!ethLoadImageFile("/tmp/ethlinks_test_odd.bin", w, err));

  f = fopen("/tmp/ethlinks_test_ok.bin", "wb");
  unsigned char b[8] = { 1, 0, 0, 0, 0x78, 0x56, 0x34, 0x12 };
  fwrite(b, 1, 8, f);
  fclose(f);
  CHECK(ethLoadImageFile("/tmp/ethlinks_test_ok.bin", w, err));
  CHECK(w.size() == 2);
  CHECK(w[0] == 1);
  CHECK(w[1] == 0x12345678u);
}

// ---- 6. EthSeamHost: registry, error paths, chipClosing ----

static std::string bringUp(EthSeamHost & h, u32 a, u32 b) {
  return h.bringUpLink(a, b, X, IMAGE, setup(1, 2), setup(2, 3), false, true);
}

static void test_host_registry() {
  Fab f;
  f.init(X);
  FILE * im = fopen(IMAGE, "wb");
  std::vector<u32> img = fakeImage();
  fwrite(img.data(), 4, img.size(), im);
  fclose(im);

  EthSeamHost & h = EthSeamHost::get();
  CHECK(h.status() == "no eth-seam links");

  h.setPortFactory(nullptr);
  CHECK(bringUp(h, 0, 1).find("factory") != std::string::npos);

  h.setPortFactory([&](u32 chip, std::string & err)
                   -> std::unique_ptr<EthPort> {
    if (chip > 1) {
      err = "no such chip";
      return nullptr;
    }
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  CHECK(bringUp(h, 0, 0) == "a link needs two different chips");
  CHECK(bringUp(h, 0, 2) == "no such chip");
  CHECK(!h.bringUpLink(0, 1, X, "/nonexistent.bin",
                       setup(1, 2), setup(2, 3), false, true).empty());
  // image / chip errors come before any write
  CHECK(f.writes == 0);

  std::string err = bringUp(h, 0, 1);
  CHECK(err.empty());
  if (!err.empty()) printf("  error: %s\n", err.c_str());
  CHECK(h.status().find("UP") != std::string::npos);
  CHECK(bringUp(h, 0, 1).find("already") != std::string::npos);

  EthHealth eh;
  std::string e2;
  u32 v = 0;
  CHECK(h.health(0, X, eh, e2));
  CHECK(eh.port == 1);
  CHECK(!h.health(0, X + 1, eh, e2));

  f.put(1, X, channelBlockAddr(3) + 4 * CW_S_FWD, 1234);
  CHECK(h.channelStat(1, X, 3, CW_S_FWD, v, e2));
  CHECK(v == 1234);
  CHECK(!h.channelStat(1, X, NCH_MAX, 0, v, e2));
  CHECK(!h.channelStat(1, X, 0, 24, v, e2));

  CHECK(h.tearDownLink(1, 0, X) == "");
  CHECK(f.get(0, X, SOFT_RESET) == SR_DEFAULT);
  CHECK(f.get(1, X, SOFT_RESET) == SR_DEFAULT);
  CHECK(h.tearDownLink(1, 0, X) == "no such link");

  // can come up again after teardown
  CHECK(bringUp(h, 0, 1).empty());

  // chip going away: link down, state restored, port dropped
  h.chipClosing(1);
  CHECK(f.get(0, X, SOFT_RESET) == SR_DEFAULT);
  CHECK(f.get(1, X, SOFT_RESET) == SR_DEFAULT);
  CHECK(f.get(0, X, TXQ2) == 0);
  CHECK(h.status() == "no eth-seam links");

  // a fresh port is made on demand
  CHECK(bringUp(h, 0, 1).empty());
  h.tearDownAll();
  CHECK(h.status() == "no eth-seam links");
  CHECK(f.get(1, X, SOFT_RESET) == SR_DEFAULT);
}

// ---- 7. seam declaration: masks, both directions, validation ----

static void test_declare_seam() {
  EthSeamHost & h = EthSeamHost::get();
  h.clearSeams();
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());
  u16 m0 = h.seamMaskFor(0);
  u16 m1 = h.seamMaskFor(1);
  CHECK(((m0 >> (4 * D4_E)) & 0xf) == 0xf);
  CHECK(((m1 >> (4 * D4_W)) & 0xf) == 0xf);
  CHECK(((m0 >> (4 * D4_W)) & 0xf) == 0);
  CHECK(((m1 >> (4 * D4_E)) & 0xf) == 0);
  CHECK(h.seamMaskFor(7) == 0);                         // unknown chip

  // already used; same chip; no links; too many; bad dir
  CHECK(!h.declareSeam(0, D4_E, 2, { 6 }).empty());
  CHECK(!h.declareSeam(0, D4_S, 0, { 3 }).empty());
  CHECK(!h.declareSeam(0, D4_S, 2, { }).empty());
  CHECK(!h.declareSeam(0, D4_S, 2, { 3, 4, 12, 13, 3 }).empty());
  CHECK(!h.declareSeam(0, 4, 2, { 3 }).empty());

  // not in the cabling list for that axis, or listed twice
  CHECK(!h.declareSeam(0, D4_S, 2, { 6 }).empty());
  CHECK(!h.declareSeam(0, D4_S, 2, { 3, 3 }).empty());
  CHECK(h.seamMaskFor(2) == 0);                         // no trace left

  // a subset in any order: link order is still the cabling order
  CHECK(h.declareSeam(0, D4_S, 2, { 12, 3 }).empty());
  EthSeam::SeamTable t2;
  EthSeam::decodeSeamMask(h.seamMaskFor(2), t2);
  CHECK(t2.n[D4_N] == 2);
  CHECK(t2.ethX[D4_N][0] == 3);
  CHECK(t2.ethX[D4_N][1] == 12);
  h.clearSeams();
  CHECK(h.seamMaskFor(0) == 0);
}

// ---- 8. planner: one real E/W seam, 4 links, chips 0 (west) and 1 ----

// Every diagonal crossing of the E/W seam, both ways: the E1 on the
// RECEIVING card must have been told the receiving hub's NoC node and
// the right storage (a corner-hub block, or an InterHub corner index).
static void check_one_diag_way(Fab & f,
                               const EthSeam::SeamTable & sender,
                               Dir4 d4,
                               u32 sendChip,
                               u32 recvChip) {
  const CellShape & cell = CELL_C2X2;
  u32 E = edgeHubCount(d4, cell);
  (void) sendChip;
  u32 seen = 0;
  for (u32 e = 0; e < E; ++e) {
    for (Slot s : { SLOT_DIAG_LO, SLOT_DIAG_HI }) {
      DiagRoute r;
      if (!diagRoute(d4, E, e, s, r)) continue;
      Assign a = { 0, 0 };
      CHECK(assign(E, sender.n[d4], e, s, a));
      u32 ethx = sender.ethX[d4][a.link];
      u32 cb = channelBlockAddr(a.channel);

      u32 rx = 0;
      u32 ry = 0;
      CHECK(edgeHubCT6(oppositeDir4(d4), r.recvEdge, cell, rx, ry));
      EthSeamHost::SeamHub hub;
      std::string err;
      CHECK(mockHub(recvChip, rx, ry, hub, err));
      u32 node = ((u32) hub.noc0y << 6) | hub.noc0x;
      u32 base = (r.recvBlock == RECV_CHUB ? hub.chubBase : hub.ihubBase)
        + r.recvIndex * RING_BYTES;
      CHECK(f.get(recvChip, ethx, cb + 4 * CW_T6NODE) == node);
      CHECK(f.get(recvChip, ethx, cb + 4 * CW_T6BASE) == base);
      ++seen;
    }
  }
  CHECK(seen == 2 * (E - 1));
}

static void check_planned_diagonals(Fab & f,
                                    const EthSeam::SeamTable & west,
                                    const EthSeam::SeamTable & east) {
  check_one_diag_way(f, west, D4_E, 0, 1);
  check_one_diag_way(f, east, D4_W, 1, 0);
}

static void test_planner() {
  Fab f;
  const u32 xs[4] = { 6, 7, 10, 11 };
  for (u32 x : xs) f.init(x);

  EthSeamHost & h = EthSeamHost::get();
  // earlier blocks left cached ports that point at their own Fab
  h.chipClosing(0);
  h.chipClosing(1);
  h.clearSeams();
  h.setPortFactory([&f](u32 chip, std::string &)
                   -> std::unique_ptr<EthPort> {
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  h.setHubLookup(mockHub);
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());

  EthChannelSetup sa;
  EthChannelSetup sb;
  CHECK(h.planLink(0, 1, 7, 2, 4, sa, sb).empty());
  CHECK(sa.channels.size() == sb.channels.size());
  CHECK(!sa.channels.empty());
  CHECK(sa.flags == 2);
  CHECK(sa.ringCars == 4);

  // swapped call order swaps the setups
  EthChannelSetup ra;
  EthChannelSetup rb;
  CHECK(h.planLink(1, 0, 7, 2, 4, ra, rb).empty());
  CHECK(ra.channels.size() == sb.channels.size());
  CHECK(ra.channels[0].t6x == sb.channels[0].t6x);
  CHECK(!h.planLink(0, 1, 8, 2, 4, sa, sb).empty());    // not a link
  CHECK(!h.planLink(0, 2, 7, 2, 4, sa, sb).empty());    // no seam

  // bring them all up, then check every edge hub's own view
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).empty());
  CHECK(h.status().find("eth11 UP") != std::string::npos);

  // channelsOf: the planned channel numbers, same on both ends, and
  // empty for a tile or chip that is not on a live link
  EthChannelSetup pa;
  EthChannelSetup pb;
  CHECK(h.planLink(0, 1, 7, 2, 4, pa, pb).empty());
  std::vector<u32> want;
  for (const EthChannel & c : pa.channels) want.push_back(c.channel);
  CHECK(!want.empty());
  CHECK(h.channelsOf(0, 7) == want);
  CHECK(h.channelsOf(1, 7) == want);
  CHECK(h.channelsOf(0, 8).empty());
  CHECK(h.channelsOf(2, 7).empty());

  // and a counter the E1 would publish can be read back through it
  u32 v = 0;
  std::string e;
  f.put(1, 7, channelBlockAddr(want[0]) + 4 * CW_S_INSEEN, 42);
  CHECK(h.channelStat(1, 7, want[0], CW_S_INSEEN, v, e));
  CHECK(v == 42);

  EthSeam::SeamTable west;
  EthSeam::SeamTable east;
  EthSeam::decodeSeamMask(h.seamMaskFor(0), west);
  EthSeam::decodeSeamMask(h.seamMaskFor(1), east);
  const CellShape & cell = CELL_C2X2;
  for (u32 hub = 0; hub < edgeHubCount(D4_E, cell); ++hub) {
    u32 y = hub * cell.ch + cell.hoy;
    DestDir4 d = { 0, 0 };
    // the west card's east-edge hub sends toward the east card
    CHECK(destDir4(west, D4_E, 13, y, cell, d));
    u32 cb = channelBlockAddr(d.channel);

    EthSeamHost::SeamHub hubW;
    EthSeamHost::SeamHub hubE;
    std::string err;
    CHECK(mockHub(0, 13, y, hubW, err));                // sender's own hub
    CHECK(mockHub(1, 1, y, hubE, err));                 // receiving hub

    // the east card's E1 delivers into hubE's W-direction storage
    u32 nodeE = ((u32) hubE.noc0y << 6) | hubE.noc0x;
    u32 baseE = hubE.ihubBase + dir4ToDir8(D4_W) * RING_BYTES;
    CHECK(f.get(1, d.ethx, cb + 4 * CW_T6NODE) == nodeE);
    CHECK(f.get(1, d.ethx, cb + 4 * CW_T6BASE) == baseE);

    // the west card's E1 delivers into hubW's E-direction storage
    u32 nodeW = ((u32) hubW.noc0y << 6) | hubW.noc0x;
    u32 baseW = hubW.ihubBase + dir4ToDir8(D4_E) * RING_BYTES;
    CHECK(f.get(0, d.ethx, cb + 4 * CW_T6NODE) == nodeW);
    CHECK(f.get(0, d.ethx, cb + 4 * CW_T6BASE) == baseW);

    // and the east card's hub sends back on the same tile and channel
    DestDir4 back = { 0, 0 };
    CHECK(destDir4(east, D4_W, 1, y, cell, back));
    CHECK(back.ethx == d.ethx);
    CHECK(back.channel == d.channel);
  }
  check_planned_diagonals(f, west, east);
  h.tearDownAll();
  CHECK(h.status() == "no eth-seam links");

  // one link cannot carry the channels: clear failure, nothing left up
  h.clearSeams();
  CHECK(h.declareSeam(0, D4_E, 1, { 6 }).empty());
  std::string err1 = h.bringUpSeams(IMAGE, 2, 4, false, true);
  CHECK(err1.find("NCH_MAX") != std::string::npos);
  CHECK(h.status() == "no eth-seam links");
  CHECK(f.get(0, 6, SOFT_RESET) == SR_DEFAULT);
  h.clearSeams();
  CHECK(h.bringUpSeams("/tmp/x", 2, 4, false, true) == "no seams declared");

  // do not leave ports (or the factory) pointing at this block's Fab
  h.chipClosing(0);
  h.chipClosing(1);
  h.setPortFactory(nullptr);
}

// The N/S seam through the same planner (cards 0 above 1 in the mock; the
// real pair is chips 0 and 2): every Dir4 and diagonal channel of each end is
// told the receiving hub across the seam.
static void test_planner_ns() {
  Fab f;
  for (u32 x : { 3u, 4u, 12u, 13u }) f.init(x);
  EthSeamHost & h = EthSeamHost::get();
  h.chipClosing(0);
  h.chipClosing(1);
  h.clearSeams();
  h.setPortFactory([&f](u32 chip, std::string &)
                   -> std::unique_ptr<EthPort> {
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  h.setHubLookup(mockHub);
  CHECK(h.declareSeam(0, D4_S, 1, { 3, 4, 12, 13 }).empty());
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).empty());
  CHECK(h.status().find("eth13 UP") != std::string::npos);

  EthSeam::SeamTable top;
  EthSeam::SeamTable bottom;
  EthSeam::decodeSeamMask(h.seamMaskFor(0), top);
  EthSeam::decodeSeamMask(h.seamMaskFor(1), bottom);
  const CellShape & cell = CELL_C2X2;
  const u32 E = edgeHubCount(D4_S, cell);
  CHECK(E == 7);
  for (u32 e = 0; e < E; ++e) {
    u32 x = 0;
    u32 y = 0;
    CHECK(edgeHubCT6(D4_S, e, cell, x, y));
    DestDir4 d = { 0, 0 };
    CHECK(destDir4(top, D4_S, x, y, cell, d));
    u32 cb = channelBlockAddr(d.channel);
    EthSeamHost::SeamHub hubB;
    std::string err;
    CHECK(mockHub(1, x, 1, hubB, err));       // bottom card's top-row hub
    u32 node = ((u32) hubB.noc0y << 6) | hubB.noc0x;
    u32 base = hubB.ihubBase + dir4ToDir8(D4_N) * RING_BYTES;
    CHECK(f.get(1, d.ethx, cb + 4 * CW_T6NODE) == node);
    CHECK(f.get(1, d.ethx, cb + 4 * CW_T6BASE) == base);
    DestDir4 back = { 0, 0 };
    CHECK(destDir4(bottom, D4_N, x, 1, cell, back));
    CHECK(back.ethx == d.ethx);
    CHECK(back.channel == d.channel);
  }
  check_one_diag_way(f, top, D4_S, 0, 1);
  check_one_diag_way(f, bottom, D4_N, 1, 0);
  h.tearDownAll();

  // two links would need 10 channels: refused, nothing left up
  h.clearSeams();
  CHECK(h.declareSeam(0, D4_S, 1, { 3, 4 }).empty());
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).find("NCH_MAX")
        != std::string::npos);
  CHECK(h.status() == "no eth-seam links");
  h.clearSeams();
  h.chipClosing(0);
  h.chipClosing(1);
  h.setPortFactory(nullptr);
}

// The whole 2x2 array (chips 0 1 / 2 3): four seams, 16 links, brought up
// together.  Every chip's HostBlock mask carries one E/W and one N/S nibble,
// every seam's Dir4 and diagonal channels land on the right far hub, and a
// seam with too few links is still refused.
// The centre-diagonal extras of one seam, as planned: the sending end has
// only LEG_OUT, the receiving end only LEG_IN_NOC, and the receiver was told
// the far corner hub's NoC node and storage.
static void check_centre_way(Fab & f,
                             const EthSeam::SeamTable & sender,
                             Dir4 d4,
                             u32 sendChip,
                             u32 recvChip) {
  const CellShape & cell = CELL_C2X2;
  u32 E = edgeHubCount(d4, cell);
  CHECK(sendsCentre(sender, d4));
  for (CentreWhich w : { CENTRE_FIRST, CENTRE_SECOND }) {
    Assign a = { 0, 0 };
    CHECK(assignCentre(E, sender.n[d4], w, a));
    u32 ethx = sender.ethX[d4][a.link];
    u32 cb = channelBlockAddr(a.channel);
    CentreRoute r = { DIAG_FIRST_HOP, 0, 0, RECV_IHUB, 0 };
    CHECK(centreRoute(d4, E, w, r));

    u32 rx = 0;
    u32 ry = 0;
    CHECK(edgeHubCT6(oppositeDir4(d4), r.recvEdge, cell, rx, ry));
    EthSeamHost::SeamHub hub;
    std::string err;
    CHECK(mockHub(recvChip, rx, ry, hub, err));
    u32 node = ((u32) hub.noc0y << 6) | hub.noc0x;
    u32 base = (r.recvBlock == RECV_CHUB ? hub.chubBase : hub.ihubBase)
      + r.recvIndex * RING_BYTES;
    CHECK(f.get(sendChip, ethx, cb + 4 * CW_LEGS) == LEG_OUT);
    CHECK(f.get(recvChip, ethx, cb + 4 * CW_LEGS) == LEG_IN_NOC);
    CHECK(f.get(recvChip, ethx, cb + 4 * CW_T6NODE) == node);
    CHECK(f.get(recvChip, ethx, cb + 4 * CW_T6BASE) == base);
  }
}

static void test_planner_four() {
  Fab f;
  for (u32 x : { 6u, 7u, 10u, 11u }) f.init(x, 1, 4);     // 0-1 and 2-3
  for (u32 x : { 3u, 4u, 12u, 13u }) f.init(x, 2, 4);     // 0-2 and 1-3
  EthSeamHost & h = EthSeamHost::get();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.clearSeams();
  h.setPortFactory([&f](u32 chip, std::string &)
                   -> std::unique_ptr<EthPort> {
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  h.setHubLookup(mockHub);
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());
  CHECK(h.declareSeam(2, D4_E, 3, { 6, 7, 10, 11 }).empty());
  CHECK(h.declareSeam(0, D4_S, 2, { 3, 4, 12, 13 }).empty());
  CHECK(h.declareSeam(1, D4_S, 3, { 3, 4, 12, 13 }).empty());
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).empty());
  u32 up = 0;
  for (u32 x : { 6u, 7u, 10u, 11u, 3u, 4u, 12u, 13u }) {
    if (h.status().find("eth" + std::to_string(x) + " UP")
        != std::string::npos) ++up;
  }
  CHECK(up == 8);

  // each chip has an E/W and an N/S seam: its mask has both nibbles
  const Dir4 ewOf[4] = { D4_E, D4_W, D4_E, D4_W };
  const Dir4 nsOf[4] = { D4_S, D4_S, D4_N, D4_N };
  const u32 ewPeer[4] = { 1, 0, 3, 2 };
  const u32 nsPeer[4] = { 2, 3, 0, 1 };
  EthSeam::SeamTable t[4];
  for (u32 c = 0; c < 4; ++c) {
    EthSeam::decodeSeamMask(h.seamMaskFor(c), t[c]);
    CHECK(t[c].n[ewOf[c]] == 4);
    CHECK(t[c].n[nsOf[c]] == 4);
    CHECK(t[c].n[oppositeDir4(ewOf[c])] == 0);
    CHECK(t[c].n[oppositeDir4(nsOf[c])] == 0);
  }
  for (u32 c = 0; c < 4; ++c) {
    check_one_diag_way(f, t[c], ewOf[c], c, ewPeer[c]);
    check_one_diag_way(f, t[c], nsOf[c], c, nsPeer[c]);
  }
  // centre extras: chip c sends on whichever of its two seams has the other
  // one clockwise of it, and exactly one of its seams does
  for (u32 c = 0; c < 4; ++c) {
    bool sendEW = sendsCentre(t[c], ewOf[c]);
    bool sendNS = sendsCentre(t[c], nsOf[c]);
    CHECK(sendEW != sendNS);
    if (sendEW) check_centre_way(f, t[c], ewOf[c], c, ewPeer[c]);
    if (sendNS) check_centre_way(f, t[c], nsOf[c], c, nsPeer[c]);
  }
  // 0 -> 1 east, 1 -> 3 south, 3 -> 2 west, 2 -> 0 north
  CHECK(sendsCentre(t[0], D4_E));
  CHECK(sendsCentre(t[1], D4_S));
  CHECK(sendsCentre(t[3], D4_W));
  CHECK(sendsCentre(t[2], D4_N));
  h.tearDownAll();
  CHECK(h.status() == "no eth-seam links");
  h.clearSeams();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.setPortFactory(nullptr);
}

// An L of three chips (0-1 east, 0-2 south, no 1-3 or 2-3) must be REFUSED:
// chip 0's corner car would make its first hop and then have nowhere to go.
static void test_planner_lshape() {
  Fab f;
  for (u32 x : { 6u, 7u, 10u, 11u }) f.init(x, 1, 4);
  for (u32 x : { 3u, 4u, 12u, 13u }) f.init(x, 2, 4);
  EthSeamHost & h = EthSeamHost::get();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.clearSeams();
  h.setPortFactory([&f](u32 chip, std::string &)
                   -> std::unique_ptr<EthPort> {
    return std::unique_ptr<EthPort>(new MockPort(f, (int) chip));
  });
  h.setHubLookup(mockHub);
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());
  CHECK(h.declareSeam(0, D4_S, 2, { 3, 4, 12, 13 }).empty());
  std::string why = h.bringUpSeams(IMAGE, 2, 4, false, true);
  CHECK(why.find("close a square at chip 0") != std::string::npos);
  CHECK(h.status() == "no eth-seam links");
  // closing the square makes it acceptable
  CHECK(h.declareSeam(1, D4_S, 3, { 3, 4, 12, 13 }).empty());
  CHECK(h.declareSeam(2, D4_E, 3, { 6, 7, 10, 11 }).empty());
  CHECK(h.bringUpSeams(IMAGE, 2, 4, false, true).empty());
  h.tearDownAll();
  // a mis-closed square: 3 sits east of 2 but 1's south neighbor is 2
  h.clearSeams();
  CHECK(h.declareSeam(0, D4_E, 1, { 6, 7, 10, 11 }).empty());
  CHECK(h.declareSeam(0, D4_S, 2, { 3, 4, 12, 13 }).empty());
  CHECK(h.declareSeam(1, D4_S, 3, { 3, 4, 12, 13 }).empty());
  CHECK(h.declareSeam(3, D4_E, 2, { 6, 7, 10, 11 }).empty());
  why = h.bringUpSeams(IMAGE, 2, 4, false, true);
  CHECK(why.find("close a square") != std::string::npos);
  h.clearSeams();
  for (u32 c = 0; c < 4; ++c) h.chipClosing(c);
  h.setPortFactory(nullptr);
}

int main() {
  ethLinksSetLogger([](const std::string & s) { logs.push_back(s); });
  ethLinksSetUpWaitMs(0);       // tests must not wait for a dead link

  test_happy_path();
  test_up_wait();
  test_verify_after_bringup();
  test_refusals();
  test_force();
  test_channel_validation();
  test_image_loader();
  test_host_registry();
  test_declare_seam();
  test_planner();
  test_planner_ns();
  test_planner_four();
  test_planner_lshape();

  printf("%zu log lines; %s\n",
         logs.size(),
         fails ? "SOME CHECKS FAILED" : "all ok");
  return fails != 0;
}
