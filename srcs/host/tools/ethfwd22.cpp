// ethfwd22.cpp -- ethfwd21 plus: the ETH node id the T6 image writes to is now correct for NIU1.  New tool version (ethfwd21 unchanged).
//   WHY: out15 (expts15, ethfwd21): the T6 -> ETH write over NoC1 delivered nothing.  Cause (tt-isa-documentation BlackholeA0/NoC/Coordinates.md,
//   NoC_BH_1_Y01Coords.svg): the NIU config has coordinate translation ON (0x6005 bit 14), so Tensix tiles share one translated system on both NoCs,
//   BUT rows Y=0 and Y=1 (PCIe/ARC and the ETH tiles) are NOT translated: X there is the RAW coordinate of the NoC in use.  On NoC1 raw X is
//   mirrored (x1 = 16 - x0) and Y is the same (row 0/1 on top).  ethfwd21 passed NoC0 (6,1), which on NoC1 is the tile E13, not our E1 (E10).
//   NOW: with --t6-noc 1 the ETH node X sent to the T6 image is 16 - ETHX (--link 0:1:6 => (10,1)).  --t6-e1x N overrides it; the unflipped
//   value (N == ETHX, the ethfwd21 behaviour) is REFUSED because it writes into a different, live ETH tile.
// ethfwd21.cpp -- ethfwd20 plus: --t6-noc 0|1 for --pp.  New tool version (ethfwd20 unchanged).  Needs the t6pp3 image (id 0x76500003; t6pp/build.sh t6pp3).
//   --t6-noc 1 makes the T6 image drive NIU1 (0xFFB30000) instead of NIU0 for its NoC writes to the ETH tile (the T6 -> ERISC leg of the ping-pong), with
//   node ids still raw NoC0 coordinates as MFMx passes them to NIU1.  T6 -> ETH over NoC1 is UNPROVEN: a misrouted write lands on another tile or nowhere
//   (a timeout or bad payload in the result).  Combine with --e1-noc 0|1 for the ETH -> T6 leg (proven both ways).  Read expts15.sh first.
// ethfwd20.cpp -- ethfwd19 with the link-health DELTA sign fixed, plus --dump-niu-after.  New tool version (ethfwd19 unchanged).
//   BUG FIXED: ethfwd19 called health_print for the 'after' snapshot with its two snapshots swapped, so every printed delta was (before - after):
//   the FEC-corrected 'decreases' in out14 were really increases (the absolute 'after' values were right).  The 0x7D040.. words are now labelled
//   'words (semantics unverified)': in out14 they look like high-entropy data, not counters.
//   --dump-niu-after: also dump the NIU0/NIU1 command-buffer registers at the end of the run (read-only).  After a --noc run the card whose E1
//   NoC-writes (card B) should show the E1's last command in the buffer it used, which proves the dump is sensitive; if it does and card A
//   (no NoC writes) and the pre-run dump are all zero, base firmware leaves no trace in any NIU command buffer.
//
// ---- ethfwd19 header follows ----
// ethfwd19.cpp -- ethfwd18 plus the e1fwd16 experiment flags, a read-only NIU dump and link-health snapshots.  New tool version (ethfwd18 unchanged).
//   --txqfix        E1 does the BH-55 workaround (dummy TXQ CMD read before every STATUS poll)        [needs the e1fwd16 image]
//   --e1-noc 0|1    E1's NoC writes (the L_IN_NOC leg) use NIU0 (default) or NIU1                     [e1fwd16]; NoC1 from an ETH tile is UNPROVEN:
//                   a misrouted write lands on another tile or nowhere (test times out).  Use the default T6 tile 1:2 for it.
//   --e1-nri-buf N  NIU command buffer (0..3, default 3) the E1 drives for its NoC writes              [e1fwd16].  CAUTION: base firmware (E0) may use
//                   some NIU0 buffers; run --dump-niu first and prefer a buffer that looks unused.
//   --dump-niu      READ-ONLY: print the NIU0/NIU1 command-buffer registers (buffers 0..3) of the ETH tile on both cards, then exit.  Run it BEFORE any E1
//                   use: non-zero buffers show what base firmware has driven.
//   ALWAYS (read-only): link-health snapshots of both cards before E1 use and at the end, with deltas: port status, syseng heartbeat 0x7CC70,
//   retrain count 0x7CE00, corrected/uncorrected FEC codewords 0x7CE90/0x7CE98, base-FW dynamic NoC counter words 0x7D040.. (tt-metal issue
//   #28366: base FW keeps software NoC counters there).  Never writes any of them (the retrain-force word is never touched).
//
// ---- ethfwd18 header follows ----
// ethfwd18.cpp -- ethfwd17 plus: --t6-hart b|nc and --nri-buf N for --pp.  New tool version (ethfwd17 unchanged).
//   Needs the t6pp3 image (t6pp/t6pp3.c; id 0x76500003; ethfwd20 needs t6pp2), which has entry points for BOTH harts and a configurable NIU command buffer.
//   --t6-hart nc starts the image on NCRISC the way MFMx does (OurTLBs::resetTheFleet): NCRISC_RESET_PC (0xFFB12238) = 16 and its
//   override (0xFFB1223C) = 1, then ONLY soft-reset bit 18 is released; both registers are restored at exit.  --t6-hart b (default) releases
//   bit 11 alone (reset address 0).  --nri-buf N (0..3, default 3 = what NRI3 uses) selects the NIU command buffer the image drives.
//   Reason: NRIUtils.cpp asserts NRI3 is for hart NC (software convention); the tool lets us find out which hart/buffer combinations work.
//
// ---- ethfwd17 header follows ----
// ethfwd17.cpp -- ethfwd16 plus stage 3a: --pp, a bare-metal T6 car ping-pong across the seam.  New tool version (ethfwd16 unchanged).
//   --pp replaces the host as car source/sink: the t6pp image (t6pp/t6pp.c, built by sh t6pp/build.sh) runs on BRISC of one Tensix tile of EACH
//   card.  Tile A seeds K cars per channel into its own L1 receive ring and NoC-writes them to its local E1's OUT ring; E1(A) ships them
//   over the link; E1(B) NoC-writes them into tile B's receive ring; tile B flips each car and sends it back the same way; tile A reflects
//   again ...  The host only configures, releases BRISC (reset bit 11, all other cores stay in reset) and samples counters.  It reports
//   round trips/s, per-car round-trip time, payload verification (every --ppve-th car, default 16) and NoC/TXQ failures.
//   Options: --pp, --t6-image FILE (the t6pp.bin, required), --t6a X:Y / --t6b X:Y (NoC0 tile coordinates on card A / card B, default 1:2
//   each; flex them: NoC route length depends on where the tiles sit), --t6-addr ADDR (receive rings, default 0x100000, nch*0x4100 bytes),
//   --ppve N.  A harvested tile makes the tool exit with code 4 ("tile unavailable") after restoring everything, not a fault.
//   PICK TILES AND A REGION NOTHING ELSE USES: the tool resets those cores, overwrites L1 0..image, 0x8000..0x83FF and the ring region, and
//   restores soft-reset at exit.  (tt-kmd NOC_CLEANUP covers only the E1; if the tool is KILLED the T6 BRISC keeps running harmlessly.)
//
// ---- ethfwd16 header follows ----
// ethfwd16.cpp -- ethfwd15 plus the NoC leg.  (ethfwd15 unchanged.)
//   NEW --noc test (needs --go and a T6 tile; not part of the default set): card A's E1 runs L_OUT on nch channels, card B's E1 runs
//   L_IN_NOC with per-channel (T6 node, T6 base = --t6-addr + c*0x4100).  The host injects cars into A's OUT rings (as a T6 on A
//   would); each car goes A E1 -> TXQ2 -> B E1 IN ring -> NIU0 NoC write -> the T6 ring of channel c; the host reads and verifies the
//   T6 L1 word for word.  Like ethfwd12's stage 2 the tool owns the T6 tile: it puts that tile's five RISC-V cores in reset
//   (restored at exit), zeroes the nch*0x4100-byte region and reads it back.  PICK A T6 REGION NOTHING USES.  --t6 X:Y is a NoC0
//   tile coordinate (default 1:2), --t6-addr a 16B-aligned L1 address (default 0x100000).
//   NEW --vcstatic: E1 sets NOC_CMD_VC_STATIC in its NoC write (needs the e1fwd15 image; id 0xE1F00015 is accepted too).
//
// ---- ethfwd15 header follows ----
// ethfwd15.cpp -- ethfwd14 with one bug fixed.  (ethfwd14 unchanged.)
//   FIX: ethfwd14 always configured the E1 ring depth (CTL+0x24) as 4.  REFLECT with K cars in flight needs the ring depth = K
//   (as ethfwd12's stage 1b did): with K<4 the single car comes back to slot 0 while E1 waits at slot 1, so K=1 and K=2 STALLED
//   (hw run 2026-11-16: K=1 stalled with both e1fwd13 and e1fwd14; K=4 ran).  e1_start now takes the ring depth; idle/route use 4,
//   reflect uses K.  Everything else is ethfwd14.
//
// ---- ethfwd14 header follows ----
// ethfwd14.cpp -- eth-seam multi-channel link tests for the e1fwd13 / e1fwd14 router images.  (ethfwd13 unchanged.)
//
// What it is: ethfwd13's plumbing (validate, queue-2/TX-header setup, E1 deploy with NOC_CLEANUP, allowlisted writes, mock) driving
// the 8-channel image layout (CTL block 0x8000..0x85FF, channel c block at 0x8100 + c*0x80, rings in L1 scratch 0x10000..0x50FFF).
// There is NO embedded image: --image FILE.bin is required (sh e1fwd/build.sh e1fwd13   or   e1fwd14).  The tool checks the image-id
// word (CTL+0x54) so it refuses to run tests against an image with a different layout.
//
// Tests (default: all, on --nch channels; the host plays the T6/hubs by reading and writing the rings over PCIe; no NoC leg yet):
//   --idle     router sweep rate with no cars (CTL+0x30 deltas): the cleanest e1fwd13 vs e1fwd14 speed comparison.
//   --route    both E1s run ROUTER with legs OUT|IN_COUNT on every channel; the host puts cars in OUT rings, E1 ships them over TXQ2,
//              the host reads and verifies the peer's IN rings word for word, both directions; E1 counters are cross-checked.
//   --reflect  both E1s run REFLECT on every channel at once; reports total hops/s and per-channel min/max (fairness).
// Not covered yet: the IN->T6 NoC leg (L_IN_NOC) and a real T6 hub; that is the next tool/stage.
//
// SAFETY: as ethfwd13: default is a DRY RUN; --go enables writes; every write is allowlisted (write_ok; the CTL window is now 0x600
// bytes); tt-kmd NOC_CLEANUP is armed per card so E1 returns to reset if this process dies.
//
// CONFIDENCE: --mock exercises only this tool's logic (the mock E1 is FAKE).  Nothing here has run on hardware.  The images' logic
// is checked natively by e1fwd/hosttest13.c / hosttest14.c.
//
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd20.cpp -o ethfwd20
// Run:   ./ethfwd20 --link 0:1:6 --image e1fwd/e1fwd14.bin                  # dry run
//        ./ethfwd20 --link 0:1:6 --image e1fwd/e1fwd14.bin --go --nch 2
// ioctl plumbing from ethlinktest14.cpp / ethdump.c (Tenstorrent, Apache-2.0: SET_NOC_CLEANUP usage).
#pragma GCC diagnostic ignored "-Wunused-function"
#include <algorithm>
#include <csignal>
#include <map>
#include <memory>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// ---- tt-kmd ioctl ABI (copied from MFMx srcs/host/include/TTKMDStuff.h,
// ---- itself from tt-kmd ioctl.h; _IO(0xFA, n) == 0xFA00|n)
constexpr unsigned TT_IOCTL_GET_DEVICE_INFO = 0xFA00;
constexpr unsigned TT_IOCTL_ALLOCATE_TLB    = 0xFA0B;
constexpr unsigned TT_IOCTL_FREE_TLB        = 0xFA0C;
constexpr unsigned TT_IOCTL_CONFIGURE_TLB   = 0xFA0D;

struct tt_get_device_info_in  { uint32_t output_size_bytes; };
struct tt_get_device_info_out {
  uint32_t output_size_bytes;
  uint16_t vendor_id, device_id, subsystem_vendor_id, subsystem_id;
  uint16_t bus_dev_fn;
  uint16_t max_dma_buf_size_log2;
  uint16_t pci_domain;
};
struct tt_get_device_info { tt_get_device_info_in in; tt_get_device_info_out out; };

struct tt_allocate_tlb_in  { uint64_t size; uint64_t reserved; };
struct tt_allocate_tlb_out { uint32_t id; uint32_t reserved0; uint64_t mmap_offset_uc; uint64_t mmap_offset_wc; uint64_t reserved1; };
struct tt_allocate_tlb { tt_allocate_tlb_in in; tt_allocate_tlb_out out; };

struct tt_free_tlb_in  { uint32_t id; };
struct tt_free_tlb_out { };
struct tt_free_tlb { tt_free_tlb_in in; tt_free_tlb_out out; };

struct tt_noc_tlb_config {
  uint64_t addr;
  uint16_t x_end, y_end, x_start, y_start;
  uint8_t noc, mcast, ordering, linked, static_vc;
  uint8_t reserved0[3];
  uint32_t reserved1[2];
};
struct tt_configure_tlb_in  { uint32_t id; tt_noc_tlb_config config; };
struct tt_configure_tlb_out { uint64_t reserved; };
struct tt_configure_tlb { tt_configure_tlb_in in; tt_configure_tlb_out out; };

constexpr uint16_t PCI_VENDOR_TENSTORRENT = 0x1E52;
constexpr uint16_t PCI_DEVICE_BLACKHOLE   = 0xB140;

constexpr uint64_t TLB_SIZE = 1ull << 21;  // 2 MiB windows
constexpr uint64_t TLB_MASK = TLB_SIZE - 1;

constexpr uint32_t ETH_BOOT_RESULTS_ADDR = 0x0007CC00;
constexpr uint32_t NIU0_ADDR             = 0xFFB20000;
constexpr uint32_t NOC_ENDPOINT_ID_OFF   = 0x048;
constexpr uint32_t NIU_CFG_0_OFF         = 0x100;
constexpr uint32_t NIU_CFG_0_HARVESTED   = 1u << 12;
constexpr uint32_t TXQ2 = 0xFFB92000, RXQ2 = 0xFFB96000, TXHDR1 = 0xFFB98280;
constexpr uint32_t SRC_L1 = 0x10000, DST_L1 = 0x40000, SCRATCH_END = 0x70000;   // [SRC,DST) src slots, [DST,END) dst slots
constexpr uint32_t RXBUF_L1 = 0x7E000;  // zero page; RXQ0 uses 0x7F000

struct Port {
  virtual ~Port() {}
  virtual bool rd32(unsigned x, uint32_t a, uint32_t& v) = 0;
  virtual bool wr32(unsigned x, uint32_t a, uint32_t v) = 0;
  virtual bool set_noc_cleanup(unsigned, uint32_t, uint32_t) { return false; }
  // tile (x,y); ETH tiles are y=1.  Default maps non-ETH tiles onto the mock/legacy x argument as x|(y<<6).
  virtual bool rd32y(unsigned x, unsigned y, uint32_t a, uint32_t& v) { return rd32(y == 1 ? x : (x | (y << 6)), a, v); }
  virtual bool wr32y(unsigned x, unsigned y, uint32_t a, uint32_t v) { return wr32(y == 1 ? x : (x | (y << 6)), a, v); }
};

// One fd per card; one 2 MiB TLB window per (tile, 2MiB-base) actually used, so alternating between
// tiles/links costs no ioctl.
struct HwPort : Port {
  struct Win { unsigned x, y; uint64_t base; uint32_t id; volatile uint8_t* p; };
  int fd = -1;
  std::vector<Win> wins;
  bool open_dev(const std::string& path, std::string& err) {
    fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) { err = "open " + path + ": " + strerror(errno); return false; }
    tt_get_device_info di; memset(&di, 0, sizeof di); di.in.output_size_bytes = sizeof di.out;
    if (ioctl(fd, TT_IOCTL_GET_DEVICE_INFO, &di) < 0) { err = "GET_DEVICE_INFO failed"; return false; }
    if (di.out.vendor_id != PCI_VENDOR_TENSTORRENT || di.out.device_id != PCI_DEVICE_BLACKHOLE) { err = "not a Blackhole"; return false; }
    return true;
  }
  ~HwPort() override {
    for (Win& w : wins) {
      munmap((void*)w.p, TLB_SIZE);
      tt_free_tlb ft; memset(&ft, 0, sizeof ft); ft.in.id = w.id; (void)ioctl(fd, TT_IOCTL_FREE_TLB, &ft);
    }
    if (fd >= 0) ::close(fd);
  }
  volatile uint32_t* map(unsigned x, uint32_t a, unsigned y = 1) {
    if (a & 3u) return nullptr;
    uint64_t base = (uint64_t)a & ~TLB_MASK; uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    for (Win& w : wins) if (w.x == x && w.y == y && w.base == base) return (volatile uint32_t*)(w.p + off);
    if (wins.size() >= 32) return nullptr;
    tt_allocate_tlb at; memset(&at, 0, sizeof at); at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) { fprintf(stderr, "ALLOCATE_TLB: %s\n", strerror(errno)); return nullptr; }
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) { fprintf(stderr, "mmap: %s\n", strerror(errno)); return nullptr; }
    tt_configure_tlb c; memset(&c, 0, sizeof c);
    c.in.id = at.out.id; c.in.config.addr = base;
    c.in.config.x_start = x; c.in.config.x_end = x; c.in.config.y_start = y; c.in.config.y_end = y;
    c.in.config.noc = 0; c.in.config.mcast = 0;
    if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) { fprintf(stderr, "CONFIGURE_TLB: %s\n", strerror(errno)); return nullptr; }
    wins.push_back({x, y, base, at.out.id, (volatile uint8_t*)p});
    return (volatile uint32_t*)((volatile uint8_t*)p + off);
  }
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override { return rd32y(x, 1, a, v); }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override { return wr32y(x, 1, a, v); }
  bool rd32y(unsigned x, unsigned y, uint32_t a, uint32_t& v) override { volatile uint32_t* p = map(x, a, y); if (!p) return false; v = *p; return true; }
  bool wr32y(unsigned x, unsigned y, uint32_t a, uint32_t v) override { volatile uint32_t* p = map(x, a, y); if (!p) return false; *p = v; return true; }
  // Ask tt-kmd to write (addr,data) to tile (x,1) when this fd closes (even on crash): puts E1 back in reset.
  bool set_noc_cleanup(unsigned x, uint32_t addr, uint32_t data) override {
    struct { uint32_t argsz, flags; uint8_t enabled, x, y, noc; uint32_t reserved0; uint64_t addr, data; } req;
    memset(&req, 0, sizeof req);
    req.argsz = sizeof req; req.enabled = 1; req.x = (uint8_t)x; req.y = 1; req.noc = 0; req.addr = addr; req.data = data;
    return ioctl(fd, 0xFA0E, &req) == 0;
  }
};

// ---- MOCK: a tiny fake of four cards (x-edge pairs 0<->1, 2<->3), enough to exercise this tool's logic ----
struct Fabric {
  std::map<uint64_t, uint32_t> m;   // key = dev<<48 | x<<40 | addr
  static uint64_t key(int d, unsigned x, uint32_t a) { return ((uint64_t)d << 56) | ((uint64_t)x << 40) | a; }
  uint32_t get(int d, unsigned x, uint32_t a) { auto it = m.find(key(d, x, a)); return it == m.end() ? 0 : it->second; }
  void put(int d, unsigned x, uint32_t a, uint32_t v) { m[key(d, x, a)] = v; }
  static uint32_t minor(int d, unsigned x) { return 0x100000u * (uint32_t)(d + 1) + x; }
  void init(unsigned x) {
    for (int d = 0; d < 4; ++d) {
      put(d, x, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, 0x00020000u | 10);
      put(d, x, ETH_BOOT_RESULTS_ADDR + 4, 1);
      uint32_t r = ETH_BOOT_RESULTS_ADDR + 4 * 240;
      put(d, x, r + 12, 0x208c47); put(d, x, r + 16, minor(d, x)); put(d, x, r + 28, 1);
      r = ETH_BOOT_RESULTS_ADDR + 4 * 248;
      put(d, x, r + 12, 0x208c47); put(d, x, r + 16, minor(d ^ 1, x)); put(d, x, r + 28, 1);
      put(d, x, TXQ2 + 0x0c, 0x5dc);
      put(d, x, 0xFFB121B0, 0x47000); put(d, x, 0xFFB14008, 0x4000); put(d, x, 0xFFB1400C, 0x7fff);
    }
  }
  static uint32_t macw0(uint32_t major, uint32_t minor) {
    return (minor & 0xff) | (((minor >> 8) & 0xff) << 8) | (((minor >> 16) & 0xff) << 16) | ((major & 0xff) << 24);
  }
  void cmd(int d, unsigned x) {
    int p = d ^ 1;
    bool txok = get(d, x, TXQ2) & 1, rxok = (get(p, x, RXQ2) & 2) != 0;
    bool da = get(d, x, TXHDR1 + 0x18) == macw0(0x208c47, minor(p, x));
    uint32_t src = get(d, x, TXQ2 + 0x14), n = get(d, x, TXQ2 + 0x18), dst = get(d, x, TXQ2 + 0x1c), mp = get(d, x, TXQ2 + 0x0c);
    put(d, x, TXQ2 + 0x30, get(d, x, TXQ2 + 0x30) + 1);
    if (txok && rxok && da && ((get(d, x, TXQ2 + 0x80) >> 8) & 15) == 1) {
      for (uint32_t i = 0; i < n; i += 4) put(p, x, dst + i, get(d, x, src + i));
      put(p, x, RXQ2 + 0x40, (get(p, x, RXQ2 + 0x40) + (n + mp - 1) / mp) & 0xff);
    }
  }
};
constexpr uint32_t E1_CODE = 0x4000, CTL = 0x8000, CTL_WIN = 0x600, SOFT_RESET = 0xFFB121B0, E1_PC = 0xFFB14008, E1_END = 0xFFB1400C;
constexpr uint32_t SR_E1 = 0x1000;
// E1 image layout (e1fwd13.c / e1fwd14.c; keep in sync): CTL block at CTL, channel c block at CHC + c*CHSTRIDE (config words 0..7,
// stat words 8..23), cars of CAR bytes, rings of RING_CARS cars.  The host places all rings in the allowlisted L1 scratch range.
constexpr uint32_t CHC = CTL + 0x100, CHSTRIDE = 0x80, CAR = 0x1040, RING_CARS = 4, RING_BYTES = RING_CARS * CAR, NCH_MAX = 8;
constexpr uint32_t ID_E1FWD13 = 0xE1F00013u, ID_E1FWD14 = 0xE1F00014u, ID_E1FWD15 = 0xE1F00015u, ID_E1FWD16 = 0xE1F00016u;
constexpr uint32_t OUT0 = 0x10000, IN0 = OUT0 + NCH_MAX * RING_BYTES;       // 0x10000.. and 0x30800.. (end 0x51000 < SCRATCH_END)
static_assert(IN0 + NCH_MAX * RING_BYTES <= SCRATCH_END, "rings must fit the allowlisted scratch range");
inline uint32_t CHB(uint32_t c) { return CHC + c * CHSTRIDE; }
inline uint32_t OUTB(uint32_t c) { return OUT0 + c * RING_BYTES; }
inline uint32_t INB(uint32_t c) { return IN0 + c * RING_BYTES; }
enum { C_OUT = 0, C_IN = 1, C_PEERIN = 2, C_T6NODE = 3, C_T6BASE = 4, C_LEGS = 5,
       S_FWD = 8, S_INSEEN = 9, S_REFL = 10, S_TXFAIL = 11, S_VCHK = 12, S_VBAD = 13, S_VWORD = 14, S_VGOT = 15,
       S_VEXP = 16, S_OUTIDX = 17, S_ININDEX = 18, S_BYTES = 19, S_NOCW = 20, S_NOCFAIL = 21, S_PARTIAL = 22, S_NOCBYTES = 23 };
enum { M_ROUTER = 5, M_REFLECT = 6 };
enum { F_FENCE = 1, F_CSR = 2, F_REGEN = 4, F_VCSTATIC = 8, F_TXQFIX = 16, F_NOC1 = 32 };
enum { L_OUT = 1, L_IN_NOC = 2, L_IN_COUNT = 4 };
static uint32_t pkt_bytes(uint32_t tcms) { return tcms == 0u ? 16u : (tcms == 1u ? 32u : (tcms - 1u) * 64u); }
static uint32_t mk_hdr(uint32_t nonce, uint32_t tcms, uint32_t state) { return 0x2cu | ((nonce & 0xffu) << 8) | (tcms << 16) | (state << 24); }
static uint32_t payload_word(uint32_t n, uint32_t w, uint32_t salt) { return (n * 0x01010101u) ^ (w * 0x9E3779B1u) ^ (salt * 0x00010001u); }

// MOCK E1 (FAKE): ROUTER with L_OUT forwards a complete OUT-ring car to the peer's IN ring the moment the host writes its header;
// REFLECT progress and sweep counts are canned.  Exercises THIS TOOL's logic only -- nothing here runs the E1 image.
struct MockPort : Port {
  Fabric& f; int dev; MockPort(Fabric& fb, int d) : f(fb), dev(d) {}
  bool running(unsigned x) { return f.get(dev, x, CTL) == 1; }
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override {
    if (x < 64 && running(x)) {
      uint32_t nch = f.get(dev, x, CTL + 0x1c);
      if (a == CTL + 0x30) f.put(dev, x, a, f.get(dev, x, a) + 5000);                                   // FAKE progress
      if (f.get(dev, x, CTL + 0x10) == M_REFLECT)
        for (uint32_t c = 0; c < nch; ++c) if (a == CHB(c) + 4 * S_REFL) f.put(dev, x, a, f.get(dev, x, a) + 1000 + c);   // FAKE progress
    }
    if (x >= 64 && a >= 0x8100 && a < 0x8300 && ((a - 0x8100) % 0x40) == 0 && f.get(dev, x, 0x8000) == 1) f.put(dev, x, a, f.get(dev, x, a) + 1000);   // FAKE progress
    v = f.get(dev, x, a); return true;
  }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override {
    f.put(dev, x, a, v);
    if (x >= 64) {                                                    // T6 tile in the mock: plain memory + canned t6pp behaviour (FAKE)
      if (a == SOFT_RESET && (!(v & 0x800) || !(v & 0x40000))) { f.put(dev, x, 0x8008, 0x7E57A11Eu); f.put(dev, x, 0x803c, 0x76500003u); }
      if (a == 0x8004 && v == 1) f.put(dev, x, 0x800c, 1);
      return true;
    }
    if (a == TXQ2 + 4 && v == 2) f.cmd(dev, x);
    if (a == SOFT_RESET && !(v & SR_E1)) { f.put(dev, x, CTL + 8, 0xE1E1E1E1u); f.put(dev, x, CTL + 0x54, ID_E1FWD14); }   // fake E1 comes up
    if (a == CTL + 4 && v == 1) f.put(dev, x, CTL + 0xC, 1);
    uint32_t mode = f.get(dev, x, CTL + 0x10), nch = f.get(dev, x, CTL + 0x1c);
    if (mode == M_ROUTER && running(x))
      for (uint32_t c = 0; c < nch && c < NCH_MAX; ++c)
        for (uint32_t i = 0; i < RING_CARS; ++i) {
          uint32_t hdr = OUTB(c) + i * CAR;
          if (a != hdr || !(f.get(dev, x, CHB(c) + 4 * C_LEGS) & L_OUT)) continue;
          uint32_t bytes = pkt_bytes((v >> 16) & 0xff), p = dev ^ 1;
          if (f.get(dev, x, a + bytes - 4) != v) continue;
          for (uint32_t b = 0; b < bytes; b += 4) f.put(p, x, INB(c) + i * CAR + b, f.get(dev, x, hdr + b));
          f.put(dev, x, CHB(c) + 4 * S_FWD, f.get(dev, x, CHB(c) + 4 * S_FWD) + 1);
          if (f.get(p, x, CHB(c) + 4 * C_LEGS) & (L_IN_NOC | L_IN_COUNT)) f.put(p, x, CHB(c) + 4 * S_INSEEN, f.get(p, x, CHB(c) + 4 * S_INSEEN) + 1);
          if (f.get(p, x, CHB(c) + 4 * C_LEGS) & L_IN_NOC) {                                  // FAKE NoC write of the car into the T6 ring
            uint32_t node = f.get(p, x, CHB(c) + 4 * C_T6NODE), tb = f.get(p, x, CHB(c) + 4 * C_T6BASE), tx = node & 63, ty = (node >> 6) & 63;
            for (uint32_t b = 0; b < bytes; b += 4) f.put(p, tx | (ty << 6), tb + i * CAR + b, f.get(dev, x, hdr + b));
            f.put(p, x, CHB(c) + 4 * S_NOCW, f.get(p, x, CHB(c) + 4 * S_NOCW) + 1);
          }
        }
    return true;
  }
};

// ---- the ONLY writes this tool may ever issue ----
std::vector<uint32_t> g_sr_origs, g_pc_origs, g_end_origs;
uint32_t g_e1_bytes = 0;
std::vector<uint32_t> g_img;   // E1 image words actually deployed (embedded default, or --image FILE)
bool in_vec(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool write_ok(uint32_t a, uint32_t v) {
  if (a == SOFT_RESET) { for (uint32_t o : g_sr_origs) if (v == o || v == (o & ~SR_E1)) return true; return false; }
  if (a == E1_PC) return v == E1_CODE || in_vec(g_pc_origs, v);
  if (a == E1_END) return v == E1_CODE + g_e1_bytes || in_vec(g_end_origs, v);
  if (a >= E1_CODE && a < E1_CODE + 0x4000 && !(a & 3)) return true;
  if (a >= CTL && a < CTL + CTL_WIN && !(a & 3)) return true;
  if (a == TXQ2 + 0x04) return v == 2;                                   // CMD: L1-write packet only
  if (a == TXQ2 + 0x00) return v == 0 || v == 1;                         // ctrl: off / TT-link
  if (a == TXQ2 + 0x14 || a == TXQ2 + 0x18 || a == TXQ2 + 0x1c || a == TXQ2 + 0x80 || a == TXQ2 + 0x84) return true;
  if (a == RXQ2 + 0x00) return v == 0 || v == 2;                         // ctrl: off / TT-link
  if (a == RXQ2 + 0x0c || a == RXQ2 + 0x10 || a == RXQ2 + 0x18) return true;
  if (a == TXHDR1 + 0x10 || a == TXHDR1 + 0x14 || a == TXHDR1 + 0x18 || a == TXHDR1 + 0x1c) return true;
  return a >= SRC_L1 && a < SCRATCH_END && !(a & 3);
}

struct Side {
  Port* p; int dev; unsigned x; std::string name;
  uint32_t major = 0, minor = 0, pmajor = 0, pminor = 0;
};
struct Link { Side A, B; std::string name; };

bool g_go = false, g_verbose = false;
unsigned g_wait_ms = 50;
int g_bad = 0;
using Clock = std::chrono::steady_clock;
double us_since(Clock::time_point t0) { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); }

// Checked write.  verbose => print each; verify => read back (not for CMD).
bool W(Side& s, uint32_t a, uint32_t v, bool verify = true) {
  if (!write_ok(a, v)) { fprintf(stderr, "INTERNAL: write 0x%08x=0x%x not allowlisted; aborting\n", a, v); exit(3); }
  if (g_verbose || !g_go) printf("  %s W dev%d X=%-2u 0x%08x <= 0x%08x%s\n", s.name.c_str(), s.dev, s.x, a, v, g_go ? "" : "   (dry-run: NOT written)");
  if (!g_go) return true;
  if (!s.p->wr32(s.x, a, v)) { printf("    write failed\n"); ++g_bad; return false; }
  if (verify && a != TXQ2 + 4) {
    uint32_t b = 0;
    if (!s.p->rd32(s.x, a, b) || b != v) { printf("    READBACK MISMATCH %s 0x%08x: wrote 0x%08x got 0x%08x\n", s.name.c_str(), a, v, b); ++g_bad; return false; }
  }
  return true;
}
// Fast path inside timed loops: checked, unprinted, unverified.
inline void Wq(Side& s, uint32_t a, uint32_t v) {
  if (!write_ok(a, v)) { fprintf(stderr, "INTERNAL: write 0x%08x not allowlisted\n", a); exit(3); }
  if (!s.p->wr32(s.x, a, v)) ++g_bad;
}
inline uint32_t R(Side& s, uint32_t a) { uint32_t v = 0; if (!s.p->rd32(s.x, a, v)) v = 0xDEADDEAD; return v; }

void mac_words(uint32_t major, uint32_t minor, uint32_t& w0, uint32_t& w1) {   // "reverse byte order" per docs
  unsigned o[6] = {(major >> 16) & 255u, (major >> 8) & 255u, major & 255u, (minor >> 16) & 255u, (minor >> 8) & 255u, minor & 255u};
  w0 = o[5] | (o[4] << 8) | (o[3] << 16) | (o[2] << 24);
  w1 = o[1] | (o[0] << 8);
}
std::string macstr(uint32_t major, uint32_t minor) {
  char b[24]; snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", (major >> 16) & 255u, (major >> 8) & 255u, major & 255u,
                       (minor >> 16) & 255u, (minor >> 8) & 255u, minor & 255u);
  return b;
}

bool validate(Side& s, bool force) {
  uint32_t ep = R(s, NIU0_ADDR + NOC_ENDPOINT_ID_OFF), cfg = R(s, NIU0_ADDR + NIU_CFG_0_OFF);
  if ((uint16_t)(ep >> 8) != 0x0200) { printf("%s: X=%u is not an ETH tile (ep=0x%08x)\n", s.name.c_str(), s.x, ep); return false; }
  if (cfg & NIU_CFG_0_HARVESTED) { printf("%s: tile harvested\n", s.name.c_str()); return false; }
  uint32_t port = R(s, ETH_BOOT_RESULTS_ADDR + 4);
  if (port != 1) { printf("%s: dev%d X=%u port status %u (need 1=Up)\n", s.name.c_str(), s.dev, s.x, port); return false; }
  uint32_t r = ETH_BOOT_RESULTS_ADDR + 4 * 240;
  if (R(s, r + 28) != 1 || R(s, r + 32 + 28) != 1) { printf("%s: chip-info records not valid\n", s.name.c_str()); return false; }
  s.major = R(s, r + 12); s.minor = R(s, r + 16);
  s.pmajor = R(s, r + 32 + 12); s.pminor = R(s, r + 32 + 16);
  bool pristine = R(s, TXQ2) == 0 && R(s, RXQ2) == 0 && R(s, TXHDR1 + 0x10) == 0 && R(s, TXHDR1 + 0x14) == 0 &&
                  R(s, TXHDR1 + 0x18) == 0 && R(s, TXHDR1 + 0x1c) == 0 && R(s, TXQ2 + 0x80) == 0 && R(s, TXQ2 + 0x84) == 0;
  printf("  %s dev%d X=%-2u MAC %s peer(FW) %s pristine=%s\n", s.name.c_str(), s.dev, s.x, macstr(s.major, s.minor).c_str(),
         macstr(s.pmajor, s.pminor).c_str(), pristine ? "yes" : "NO");
  if (!pristine && !force) { printf("  refusing: queue-2/TXHDR[1] already in use (use --force to override)\n"); return false; }
  return true;
}

// ---- data helpers ----
uint32_t pat(int dev, uint32_t tag, uint32_t j, uint32_t w) { return (tag << 24) ^ ((uint32_t)dev << 20) ^ (j << 12) ^ w ^ 0x5A5A0000u; }

void fill_src(Side& tx, uint32_t S, uint32_t Wn, uint32_t tag) {
  for (uint32_t j = 0; j < Wn; ++j)
    for (uint32_t w = 0; w < S / 4; ++w) Wq(tx, SRC_L1 + j * S + 4 * w, pat(tx.dev, tag, j, w));
}
void fill_dst(Side& rx, uint32_t S, uint32_t Wn) {
  for (uint32_t j = 0; j < Wn; ++j)
    for (uint32_t w = 0; w < S / 4; ++w) Wq(rx, DST_L1 + j * S + 4 * w, 0xA5A5A5A5u);
}
uint32_t check_dst(Side& tx, Side& rx, uint32_t S, uint32_t Wn, uint32_t tag, uint32_t* firstBad = nullptr) {
  uint32_t good = 0;
  for (uint32_t j = 0; j < Wn; ++j) {
    bool ok = true;
    for (uint32_t w = 0; w < S / 4; ++w) {
      uint32_t got = R(rx, DST_L1 + j * S + 4 * w);
      if (got != pat(tx.dev, tag, j, w)) { if (ok && firstBad && !*firstBad) *firstBad = j * S + 4 * w + 1; ok = false; break; }
    }
    good += ok;
  }
  return good;
}

uint32_t maxpkt(Side& tx) { uint32_t m = R(tx, TXQ2 + 0x0c); return m ? m : 1500; }
uint32_t pkts_for(Side& tx, uint32_t S) { uint32_t m = maxpkt(tx); return (S + m - 1) / m; }

inline bool wait_idle(Side& tx) { for (int i = 0; i < 100000; ++i) if (!(R(tx, TXQ2 + 0x08) & 0x10000)) return true; return false; }
inline void issue(Side& tx, uint32_t src, uint32_t dst, bool setAddr) {
  if (setAddr) { Wq(tx, TXQ2 + 0x14, src); Wq(tx, TXQ2 + 0x1c, dst); }
  if (!wait_idle(tx)) { ++g_bad; return; }   // never write CMD while the previous one is still ongoing
  Wq(tx, TXQ2 + 0x04, 2);
}

// Wait until rx seq (mod 256) advanced by exp from base, confirmed stable on two reads.  Returns elapsed us or -1.
double wait_rx(Side& rx, uint32_t base, uint32_t exp, unsigned timeout_ms, Clock::time_point t0) {
  auto lim = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < lim) {
    if (((R(rx, RXQ2 + 0x40) - base) & 0xff) == (exp & 0xff)) {
      double t = us_since(t0);
      std::this_thread::sleep_for(std::chrono::microseconds(100));
      if (((R(rx, RXQ2 + 0x40) - base) & 0xff) == (exp & 0xff)) return t;
    }
  }
  return -1;
}

// Send Wn packets of S bytes with fresh tags, verify content.  Returns number of good slots.
uint32_t tagged_window(Side& tx, Side& rx, uint32_t S, uint32_t Wn, uint32_t tag, double* us) {
  fill_src(tx, S, Wn, tag); fill_dst(rx, S, Wn);
  (void)R(tx, SRC_L1); (void)R(rx, DST_L1);   // flush posted writes
  uint32_t rb = R(rx, RXQ2 + 0x40), exp = Wn * pkts_for(tx, S);
  W(tx, TXQ2 + 0x18, S);
  W(tx, TXQ2 + 0x80, 1u << 8);   // header entry 1 (unicast) -- NEVER send with entry 0 (broadcast -> base-FW RXQ0)
  auto t0 = Clock::now();
  for (uint32_t j = 0; j < Wn; ++j) issue(tx, SRC_L1 + j * S, DST_L1 + j * S, true);
  double t = wait_rx(rx, rb, exp, 500, t0);
  if (us) *us = t;
  std::this_thread::sleep_for(std::chrono::milliseconds(g_wait_ms));
  uint32_t fb = 0, good = check_dst(tx, rx, S, Wn, tag, &fb);
  if (good != Wn) printf("    tagged window %s->%s S=%u: %u/%u slots OK%s (first bad byte offset %u, got %08x)\n", tx.name.c_str(), rx.name.c_str(), S, good,
                         Wn, t < 0 ? ", RX seq never reached expected" : "", fb ? fb - 1 : 0, fb ? R(rx, DST_L1 + fb - 1) : 0u);
  return good;
}
// ---- E1 lifecycle (one E1State per card) ----
volatile sig_atomic_t g_abort = 0;
void on_sigint(int) { g_abort = 1; }

struct E1State { bool deployed = false; Side* s = nullptr; uint32_t sr = 0xFFFFFFFFu, pc = 0, end = 0; };

bool e1_deploy(Side& s, E1State& st, bool force) {
  st.sr = R(s, SOFT_RESET); st.pc = R(s, E1_PC); st.end = R(s, E1_END);
  g_e1_bytes = (uint32_t)(g_img.size() * 4);
  if (!in_vec(g_sr_origs, st.sr)) g_sr_origs.push_back(st.sr);
  if (!in_vec(g_pc_origs, st.pc)) g_pc_origs.push_back(st.pc);
  if (!in_vec(g_end_origs, st.end)) g_end_origs.push_back(st.end);
  printf("  %s: soft-reset=0x%08x (E1 %s)  E1 reset PC=0x%x end PC=0x%x\n", s.name.c_str(), st.sr, (st.sr & SR_E1) ? "in reset" : "RUNNING", st.pc, st.end);
  if (!(st.sr & SR_E1) && !force) { printf("  refusing: E1 is not in reset (someone else is using it); --force to override\n"); return false; }
  printf("  deploying %u-byte E1 image at L1 0x%x on %s\n", g_e1_bytes, E1_CODE, s.name.c_str());
  if (!g_go) {
    printf("  (dry-run) would: write image, zero control block 0x%x..0x%x, set E1 PC=0x%x END=0x%x, arm NOC_CLEANUP(0x%x<=0x%x), clear soft-reset bit 12\n",
           CTL, CTL + CTL_WIN - 1, E1_CODE, E1_CODE + g_e1_bytes, SOFT_RESET, st.sr);
    return true;
  }
  for (uint32_t a = CTL; a < CTL + CTL_WIN; a += 4) Wq(s, a, 0);
  for (size_t i = 0; i < g_img.size(); ++i) Wq(s, E1_CODE + 4 * (uint32_t)i, g_img[i]);
  unsigned bad = 0;
  for (size_t i = 0; i < g_img.size(); ++i) bad += (R(s, E1_CODE + 4 * (uint32_t)i) != g_img[i]);
  if (bad) { printf("  image readback mismatch in %u words; aborting\n", bad); return false; }
  Wq(s, E1_PC, E1_CODE); Wq(s, E1_END, E1_CODE + g_e1_bytes);
  bool armed = s.p->set_noc_cleanup(s.x, SOFT_RESET, st.sr);
  printf("  NOC_CLEANUP (kernel puts E1 back in reset if we die): %s\n", armed ? "armed" : "NOT available (old tt-kmd?) -- this tool still resets E1 on normal exit");
  st.deployed = true; st.s = &s;
  Wq(s, SOFT_RESET, st.sr & ~SR_E1);          // release E1 only; other bits untouched
  for (int i = 0; i < 2000; ++i) { if (R(s, CTL + 8) == 0xE1E1E1E1u) { printf("  E1 up on %s (ready word seen after ~%d ms)\n", s.name.c_str(), i / 20); return true; } std::this_thread::sleep_for(std::chrono::microseconds(50)); }
  printf("  E1 did NOT come up on %s (no ready word)\n", s.name.c_str());
  return false;
}

void e1_reset(E1State& st) {
  if (!st.deployed || !st.s) return;
  Side& s = *st.s;
  Wq(s, SOFT_RESET, st.sr); Wq(s, E1_PC, st.pc); Wq(s, E1_END, st.end);
  printf("  E1 back in reset on %s: soft-reset now 0x%08x, E1 PC=0x%x END=0x%x\n", s.name.c_str(), R(s, SOFT_RESET), R(s, E1_PC), R(s, E1_END));
  st.deployed = false;
}

unsigned g_e1buf = 3;   // --e1-nri-buf (NIU command buffer for the E1's NoC writes)
// ---- E1 control for the e1fwd13/14 layout ----
inline uint32_t stat(Side& s, uint32_t c, uint32_t k) { return R(s, CHB(c) + 4 * k); }
inline void e1_go(Side& s) { (void)R(s, CTL); Wq(s, CTL + 0x00, 1); }
bool e1_wait_done(Side& s) {
  bool done = false;
  for (int i = 0; i < 20000 && !done; ++i) { done = R(s, CTL + 0x0c) == 1; if (!done) std::this_thread::sleep_for(std::chrono::microseconds(50)); }
  Wq(s, CTL + 0x00, 0);
  return done;
}
bool e1_stop2(Side& a, Side& b) { Wq(a, CTL + 4, 1); Wq(b, CTL + 4, 1); bool d1 = e1_wait_done(a), d2 = e1_wait_done(b); return d1 && d2; }

// Configure (but do not start) nch channels.  Config words must be final before go (e1fwd14 copies them to local RAM at start).
// Rings are at the SAME addresses on both cards; OUT of one card is delivered to IN of the peer.  Only each ring's slot HEADER
// words are zeroed: a zero header can never look like a complete car.
void e1_start(Side& s, uint32_t mode, uint32_t flags, uint32_t nch, uint32_t legs, uint32_t ve, uint32_t nr, uint32_t t6node = 0, uint32_t t6base0 = 0) {
  for (uint32_t a = CTL + 0x28; a < CTL + CTL_WIN; a += 4) if (a != CTL + 0x54) Wq(s, a, 0);   // stats/config/results; not ready(+8) or image id
  Wq(s, CTL + 0x04, 0); Wq(s, CTL + 0x0c, 0);
  Wq(s, CTL + 0x58, g_e1buf == 0 ? 0xFFu : g_e1buf);                                         // NIU command buffer for the E1's NoC writes (e1fwd16)
  Wq(s, CTL + 0x10, mode); Wq(s, CTL + 0x18, flags); Wq(s, CTL + 0x1c, nch); Wq(s, CTL + 0x20, ve); Wq(s, CTL + 0x24, nr);
  Wq(s, CTL + 0x44, (1u << 6) | s.x);                                                          // this tile's NoC node id
  for (uint32_t c = 0; c < nch; ++c) {
    uint32_t cb = CHB(c);
    Wq(s, cb + 4 * C_OUT, OUTB(c)); Wq(s, cb + 4 * C_IN, INB(c)); Wq(s, cb + 4 * C_PEERIN, INB(c));
    Wq(s, cb + 4 * C_T6NODE, t6node); Wq(s, cb + 4 * C_T6BASE, t6node ? t6base0 + c * RING_BYTES : 0); Wq(s, cb + 4 * C_LEGS, legs);
    for (uint32_t i = 0; i < RING_CARS; ++i) { Wq(s, OUTB(c) + i * CAR, 0); Wq(s, INB(c) + i * CAR, 0); }
  }
}

uint32_t tcms_for(uint32_t bytes) { return bytes == 16 ? 0 : bytes == 32 ? 1 : bytes / 64 + 1; }
bool size_ok(uint32_t bytes) { return bytes == 16 || bytes == 32 || (bytes >= 64 && bytes % 64 == 0 && bytes <= 4160); }
uint32_t salt_of(int dir, uint32_t c, uint32_t i) { return ((uint32_t)dir << 8) | (c << 2) | i; }

// write a complete car at 'slot' on s, header written last (posted writes through one mapping stay ordered)
void put_car(Side& s, uint32_t slot, uint32_t nonce, uint32_t tcms, uint32_t state, uint32_t salt) {
  uint32_t bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
  uint32_t h = mk_hdr(nonce, tcms, state);
  for (uint32_t w = 1; w < words; ++w) { if (w == fi || w == ai) continue; Wq(s, slot + 4 * w, payload_word(nonce, w, salt)); }
  if (tcms >= 2) Wq(s, slot + 4 * ai, h);
  Wq(s, slot + 4 * fi, h);
  (void)R(s, slot + 4 * fi);                   // flush: payload+footer visible before the header
  Wq(s, slot, h);
}
// returns 0 ok else (word index + 1)
uint32_t verify_car(Side& s, uint32_t slot, uint32_t nonce, uint32_t tcms, uint32_t state, uint32_t salt) {
  uint32_t bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi, h = mk_hdr(nonce, tcms, state);
  if (R(s, slot) != h) return 1;
  for (uint32_t w = 1; w < words; ++w) {
    uint32_t exp = (w == fi || w == ai) ? h : payload_word(nonce, w, salt);
    if (R(s, slot + 4 * w) != exp) return w + 1;
  }
  return 0;
}

// ---- TEST idle: E1 sweep rate with no traffic (the number e1fwd13 vs e1fwd14 differ in) ----
bool test_idle(Side& A, Side& B, const std::vector<uint32_t>& nchs, unsigned ms, uint32_t flags) {
  printf("\n--- TEST idle: router sweep rate with no cars, %u ms per point ---\n", ms);
  bool all = true;
  for (uint32_t n : nchs) {
    if (g_abort) return false;
    for (Side* s : {&A, &B}) { e1_start(*s, M_ROUTER, flags, n, L_OUT | L_IN_COUNT, 16, RING_CARS); e1_go(*s); }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint32_t a0 = R(A, CTL + 0x30), b0 = R(B, CTL + 0x30); auto t0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    uint32_t a1 = R(A, CTL + 0x30), b1 = R(B, CTL + 0x30); double secs = us_since(t0) / 1e6;
    bool d = e1_stop2(A, B);
    double sa = (double)(a1 - a0), sb = (double)(b1 - b0);
    printf("  nch=%u: A %.3f M sweeps/s (%.0f ns/sweep)  B %.3f M sweeps/s (%.0f ns/sweep)%s%s\n", n, sa / secs / 1e6, sa > 0 ? secs / sa * 1e9 : 0.0,
           sb / secs / 1e6, sb > 0 ? secs / sb * 1e9 : 0.0, d ? "" : "  (E1 stop not acked!)", (sa > 0 && sb > 0) ? "" : "  STALLED");
    if (!d || sa == 0 || sb == 0) all = false;
  }
  return all;
}

// ---- TEST route: host plays the hubs on every channel, both directions (stage 1a generalised) ----
bool test_route(Side& A, Side& B, const std::vector<uint32_t>& sizes, uint32_t nch, uint32_t rounds, uint32_t flags) {
  printf("\n--- TEST route: host-as-hub, %u channel%s, both directions, 4-car bursts, %u rounds per size ---\n", nch, nch > 1 ? "s" : "", rounds);
  bool all = true;
  for (uint32_t bytes : sizes) {
    if (g_abort) break;
    uint32_t tcms = tcms_for(bytes), chunks = (bytes + 1455) / 1456;
    for (Side* s : {&A, &B}) { e1_start(*s, M_ROUTER, flags, nch, L_OUT | L_IN_COUNT, 16, RING_CARS); e1_go(*s); }
    uint32_t bad = 0, timeouts = 0, fr = 0, fw = 0, fc = 0; int fdir = -1;
    auto t0 = Clock::now();
    for (uint32_t r = 0; r < rounds && !g_abort; ++r) {
      auto nonce = [&](uint32_t c, uint32_t i) { return ((r * 7 + i * 37 + c * 11) % 255) + 1; };
      for (uint32_t i = 0; i < RING_CARS; ++i) for (uint32_t c = 0; c < nch; ++c) {
        put_car(A, OUTB(c) + i * CAR, nonce(c, i), tcms, 3, salt_of(0, c, i));
        put_car(B, OUTB(c) + i * CAR, nonce(c, i), tcms, 3, salt_of(1, c, i));
      }
      for (uint32_t i = 0; i < RING_CARS; ++i) for (uint32_t c = 0; c < nch; ++c) for (int dir = 0; dir < 2; ++dir) {
        Side& dst = dir ? A : B;
        uint32_t want = mk_hdr(nonce(c, i), tcms, 3);
        auto lim = Clock::now() + std::chrono::milliseconds(200);
        bool got = false;
        while (Clock::now() < lim) { if (R(dst, INB(c) + i * CAR) == want) { got = true; break; } }
        if (!got) { ++timeouts; if (fdir < 0) { fr = r + 1; fdir = dir; fc = c; } continue; }
        uint32_t vb = verify_car(dst, INB(c) + i * CAR, nonce(c, i), tcms, 3, salt_of(dir ? 1 : 0, c, i));   // sender dir 0 = A
        if (vb) { ++bad; if (fdir < 0) { fr = r + 1; fdir = dir; fw = vb - 1; fc = c; } }
      }
      if (timeouts > 20) break;
    }
    double secs = us_since(t0) / 1e6;
    (void)e1_stop2(A, B);
    uint32_t n = rounds * RING_CARS;
    printf("  size %4u B (tcms %2u, %u chunk%s) x %u ch: %u cars/dir/ch expected; bad %u, timeouts %u; %.3f s (host PCIe-dominated)\n", bytes, tcms, chunks,
           chunks > 1 ? "s" : "", nch, n, bad, timeouts, secs);
    bool ok = !bad && !timeouts;
    for (uint32_t c = 0; c < nch; ++c) {
      uint32_t fa = stat(A, c, S_FWD), fb = stat(B, c, S_FWD), ia = stat(A, c, S_INSEEN), ib = stat(B, c, S_INSEEN), tf = stat(A, c, S_TXFAIL) + stat(B, c, S_TXFAIL);
      bool cok = fa == n && fb == n && ia == n && ib == n && !tf;
      if (g_verbose || !cok) printf("    ch%u E1: A fwd %u in-seen %u | B fwd %u in-seen %u | txfail %u | partial %u/%u%s\n", c, fa, ia, fb, ib, tf, stat(A, c, S_PARTIAL), stat(B, c, S_PARTIAL), cok ? "" : "   <-- MISMATCH (want all = n)");
      ok = ok && cok;
    }
    if (fdir >= 0) printf("    first problem: round %u ch %u dir %s word %u\n", fr, fc, fdir ? "B->A" : "A->B", fw);
    printf("    -> %s\n", ok ? "OK" : "FAIL");
    all = all && ok;
    if (!ok) break;
  }
  return all;
}

// ---- TEST reflect: autonomous ping-pong on nch channels at once (stage 1b generalised), with per-channel fairness ----
bool test_reflect(Side& A, Side& B, const std::vector<uint32_t>& sizes, const std::vector<uint32_t>& ks, uint32_t nch, unsigned dur_ms, bool regen, uint32_t ve, uint32_t flags) {
  printf("\n--- TEST reflect: autonomous ping-pong on %u channel%s (host only seeds cars and samples counters), %u ms per point%s ---\n", nch, nch > 1 ? "s" : "", dur_ms,
         regen ? ", payload regen+verify ON" : "");
  bool all = true;
  uint32_t fl = flags | (regen ? F_REGEN : 0);
  for (uint32_t bytes : sizes) for (uint32_t K : ks) {
    if (g_abort) return false;
    uint32_t tcms = tcms_for(bytes);
    for (Side* s : {&A, &B}) { e1_start(*s, M_REFLECT, fl, nch, 0, ve, K); e1_go(*s); }
    for (uint32_t c = 0; c < nch; ++c) for (uint32_t i = 0; i < K; ++i) put_car(A, INB(c) + i * CAR, i + 1, tcms, 3, (c << 2) | i);   // E1 regen salt = (c<<2)|i
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::vector<uint32_t> a0(nch), b0(nch), a1(nch), b1(nch);
    for (uint32_t c = 0; c < nch; ++c) { a0[c] = stat(A, c, S_REFL); b0[c] = stat(B, c, S_REFL); }
    uint32_t sw0 = R(A, CTL + 0x30); auto t0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(dur_ms));
    for (uint32_t c = 0; c < nch; ++c) { a1[c] = stat(A, c, S_REFL); b1[c] = stat(B, c, S_REFL); }
    uint32_t sw1 = R(A, CTL + 0x30); double secs = us_since(t0) / 1e6;
    bool d = e1_stop2(A, B);
    double hops = 0, mn = 1e300, mx = 0; uint32_t tf = 0, vb = 0, vc = 0;
    for (uint32_t c = 0; c < nch; ++c) {
      double h = (double)(a1[c] - a0[c]) + (double)(b1[c] - b0[c]);
      hops += h; mn = std::min(mn, h); mx = std::max(mx, h);
      tf += stat(A, c, S_TXFAIL) + stat(B, c, S_TXFAIL); vb += stat(A, c, S_VBAD) + stat(B, c, S_VBAD); vc += stat(A, c, S_VCHK) + stat(B, c, S_VCHK);
    }
    printf("  size %4u B K=%u x %u ch: %.3f M hops/s total => %.0f ns/hop aggregate, %.1f MB/s; per-channel hops min %.0f max %.0f; A sweeps/s %.2f M; txfail %u%s%s", bytes, K, nch,
           hops / secs / 1e6, hops > 0 ? secs / hops * 1e9 : 0.0, hops * bytes / secs / 1e6, mn, mx, (sw1 - sw0) / secs / 1e6, tf, d ? "" : "  (E1 stop not acked!)", hops > 0 ? "" : "  STALLED");
    if (regen) printf("  verify: checks %u BAD %u", vc, vb);
    printf("\n");
    if (regen && vb) for (uint32_t c = 0; c < nch; ++c) {
      if (stat(A, c, S_VBAD)) printf("    ch%u first bad on A: word %u got %08x exp %08x\n", c, stat(A, c, S_VWORD), stat(A, c, S_VGOT), stat(A, c, S_VEXP));
      if (stat(B, c, S_VBAD)) printf("    ch%u first bad on B: word %u got %08x exp %08x\n", c, stat(B, c, S_VWORD), stat(B, c, S_VGOT), stat(B, c, S_VEXP));
    }
    if (hops == 0 || mn == 0 || tf || vb || !d) all = false;
  }
  return all;
}

void restore(Side& s) {
  printf("  restoring %s (dev%d X=%u)\n", s.name.c_str(), s.dev, s.x);
  W(s, TXQ2 + 0x00, 0); W(s, RXQ2 + 0x00, 0);
  W(s, TXQ2 + 0x80, 0); W(s, TXQ2 + 0x84, 0);
  W(s, TXHDR1 + 0x10, 0); W(s, TXHDR1 + 0x14, 0); W(s, TXHDR1 + 0x18, 0); W(s, TXHDR1 + 0x1c, 0);
}

bool parse_u(const char* s, unsigned& o) { char* e = nullptr; unsigned long v = strtoul(s, &e, 0); if (e == s || *e) return false; o = (unsigned)v; return true; }
struct LinkSpec { int a, b; unsigned x; };
std::vector<uint32_t> parse_list(char* s) { std::vector<uint32_t> v; for (char* t = strtok(s, ","); t; t = strtok(nullptr, ",")) v.push_back((uint32_t)strtoul(t, nullptr, 0)); return v; }

// ---- NoC leg: T6 tile ownership (ported from ethfwd13 stage 2; region = nch rings) ----
struct T6 { bool on = false; unsigned x = 1, y = 2; uint32_t base = 0x100000, span = 0x4100; };
T6 g_t6;
constexpr uint32_t T6_SOFT_RESET = 0xFFB121B0, T6_ALL_RESET = 0x47800;   // BRISC 0x800, TRISC0-2 0x7000, NCRISC 0x40000
uint32_t g_t6_sr_orig = 0xFFFFFFFFu;
inline uint32_t R6(Side& s, uint32_t off) { uint32_t v = 0; if (!s.p->rd32y(g_t6.x, g_t6.y, g_t6.base + off, v)) v = 0xDEADDEAD; return v; }
void W6(Side& s, uint32_t off, uint32_t v) {      // host writes to the T6 ring: only inside [base, base+4*CAR), only if --go
  if (!g_t6.on || off >= g_t6.span || (off & 3)) { fprintf(stderr, "INTERNAL: T6 write offset 0x%x refused\n", off); exit(3); }
  if (!g_go) return;
  if (!s.p->wr32y(g_t6.x, g_t6.y, g_t6.base + off, v)) ++g_bad;
}
bool t6_zero(Side& s) {   // zero the whole region and read it back
  for (uint32_t off = 0; off < g_t6.span; off += 4) W6(s, off, 0);
  if (!g_go) return true;
  for (uint32_t off = 0; off < g_t6.span; off += 4) if (R6(s, off) != 0) { printf("  T6 zero readback FAILED at +0x%x\n", off); return false; }
  return true;
}
void t6_clear(Side& s) { (void)t6_zero(s); }
bool t6_sr_write(Side& s, uint32_t v) {          // allowlisted: only {orig, orig|ALL}
  if (g_t6_sr_orig == 0xFFFFFFFFu || (v != g_t6_sr_orig && v != (g_t6_sr_orig | T6_ALL_RESET))) { fprintf(stderr, "INTERNAL: T6 soft-reset write 0x%x refused\n", v); exit(3); }
  if (!g_go) return true;
  uint32_t b = 0;
  if (!s.p->wr32y(g_t6.x, g_t6.y, T6_SOFT_RESET, v) || !s.p->rd32y(g_t6.x, g_t6.y, T6_SOFT_RESET, b) || b != v) { printf("  T6 soft-reset write/readback failed (wrote 0x%x read 0x%x)\n", v, b); ++g_bad; return false; }
  return true;
}
bool t6_prepare(Side& s) {
  uint32_t ep = 0, cfg = 0, sr = 0;
  s.p->rd32y(g_t6.x, g_t6.y, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, ep); s.p->rd32y(g_t6.x, g_t6.y, NIU0_ADDR + NIU_CFG_0_OFF, cfg); s.p->rd32y(g_t6.x, g_t6.y, T6_SOFT_RESET, sr);
  g_t6_sr_orig = sr;
  printf("  T6 (NoC0 %u,%u) on card %d: NIU endpoint id 0x%08x, NIU cfg 0x%08x%s, soft-reset 0x%08x (RISC-V cores %s)\n", g_t6.x, g_t6.y, s.dev, ep, cfg,
         (cfg & NIU_CFG_0_HARVESTED) ? " HARVESTED" : "", sr, (sr & T6_ALL_RESET) == T6_ALL_RESET ? "all in reset" : "NOT all in reset");
  if (cfg & NIU_CFG_0_HARVESTED) { printf("  refusing: tile harvested\n"); return false; }
  if ((sr & T6_ALL_RESET) != T6_ALL_RESET) {
    printf("  %s: putting the tile's 5 RISC-V cores in reset (soft-reset 0x%08x -> 0x%08x); restored at exit\n", g_go ? "doing" : "(dry-run) would be", sr, sr | T6_ALL_RESET);
    if (!t6_sr_write(s, sr | T6_ALL_RESET)) return false;
  }
  printf("  region L1 0x%x..0x%x currently: 0x%08x 0x%08x ...; %s\n", g_t6.base, g_t6.base + g_t6.span, R6(s, 0), R6(s, 4), g_go ? "zeroing it (full readback)" : "(dry-run) would zero it");
  return t6_zero(s);
}
void t6_restore(Side& s) {
  if (!g_t6.on || g_t6_sr_orig == 0xFFFFFFFFu || !g_go) return;
  if (t6_sr_write(s, g_t6_sr_orig)) printf("  T6 (%u,%u) soft-reset restored to 0x%08x\n", g_t6.x, g_t6.y, g_t6_sr_orig);
}

// verify T6 slot (channel c, slot i) against the route pattern (sender = card A = dir 0); returns 0 ok else word+1
uint32_t verify_t6(Side& s, uint32_t c, uint32_t i, uint32_t nonce, uint32_t tcms) {
  uint32_t off = c * RING_BYTES + i * CAR;
  uint32_t bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi, h = mk_hdr(nonce, tcms, 3);
  if (R6(s, off) != h) return 1;
  for (uint32_t w = 1; w < words; ++w) {
    uint32_t exp = (w == fi || w == ai) ? h : payload_word(nonce, w, salt_of(0, c, i));
    if (R6(s, off + 4 * w) != exp) return w + 1;
  }
  return 0;
}

// ---- TEST noc: host -> A OUT rings -> A E1 -> link -> B E1 IN rings -> NoC -> T6 rings (per channel), host verifies the T6 L1 ----
bool test_noc(Side& A, Side& B, const std::vector<uint32_t>& sizes, uint32_t nch, uint32_t rounds, uint32_t flags) {
  printf("\n--- TEST noc: host -> A OUT -> link -> B E1 -> NoC%s -> T6 (%u,%u) rings @0x%x, %u channel%s, %u rounds per size ---\n",
         (flags & F_VCSTATIC) ? " (VC_STATIC)" : "", g_t6.x, g_t6.y, g_t6.base, nch, nch > 1 ? "s" : "", rounds);
  bool all = true;
  uint32_t node = (g_t6.y << 6) | g_t6.x;
  for (uint32_t bytes : sizes) {
    if (g_abort) break;
    uint32_t tcms = tcms_for(bytes), chunks = (bytes + 1455) / 1456;
    e1_start(A, M_ROUTER, flags, nch, L_OUT, 16, RING_CARS);
    e1_start(B, M_ROUTER, flags, nch, L_IN_NOC, 16, RING_CARS, node, g_t6.base);
    if (!t6_zero(B)) return false;
    e1_go(A); e1_go(B);
    uint32_t bad = 0, timeouts = 0, fr = 0, fw = 0, fc = 0;
    auto t0 = Clock::now();
    for (uint32_t r = 0; r < rounds && !g_abort; ++r) {
      auto nonce = [&](uint32_t c, uint32_t i) { return ((r * 7 + i * 37 + c * 11) % 255) + 1; };
      for (uint32_t i = 0; i < RING_CARS; ++i) for (uint32_t c = 0; c < nch; ++c) put_car(A, OUTB(c) + i * CAR, nonce(c, i), tcms, 3, salt_of(0, c, i));
      for (uint32_t i = 0; i < RING_CARS; ++i) for (uint32_t c = 0; c < nch; ++c) {
        uint32_t want = mk_hdr(nonce(c, i), tcms, 3);
        auto lim = Clock::now() + std::chrono::milliseconds(200);
        bool got = false;
        while (Clock::now() < lim) { if (R6(B, c * RING_BYTES + i * CAR) == want) { got = true; break; } }
        if (!got) { ++timeouts; if (!fr) { fr = r + 1; fc = c; } continue; }
        uint32_t vb = verify_t6(B, c, i, nonce(c, i), tcms);
        if (vb) { ++bad; if (!fr) { fr = r + 1; fw = vb - 1; fc = c; } }
      }
      if (timeouts > 20) break;
    }
    double secs = us_since(t0) / 1e6;
    (void)e1_stop2(A, B);
    uint32_t n = rounds * RING_CARS;
    printf("  size %4u B (tcms %2u, %u chunk%s) x %u ch: %u cars/ch expected; bad %u, timeouts %u; %.3f s (host PCIe-dominated)\n", bytes, tcms, chunks, chunks > 1 ? "s" : "", nch, n, bad, timeouts, secs);
    bool ok = !bad && !timeouts;
    for (uint32_t c = 0; c < nch; ++c) {
      uint32_t fa = stat(A, c, S_FWD), nw = stat(B, c, S_NOCW), ins = stat(B, c, S_INSEEN), nf = stat(B, c, S_NOCFAIL), tf = stat(A, c, S_TXFAIL);
      bool cok = fa == n && nw == n && ins == n && !nf && !tf;
      if (g_verbose || !cok) printf("    ch%u: A fwd %u txfail %u | B noc-writes %u in-seen %u noc-fail %u partial %u%s\n", c, fa, tf, nw, ins, nf, stat(B, c, S_PARTIAL), cok ? "" : "   <-- MISMATCH (want fwd=noc-writes=in-seen=n)");
      ok = ok && cok;
    }
    if (fr) printf("    first problem: round %u ch %u word %u\n", fr, fc, fw);
    printf("    -> %s\n", ok ? "OK" : "FAIL");
    all = all && ok;
    if (!ok) break;
  }
  return all;
}

// ---- TEST pp (stage 3a): bare-metal T6 car ping-pong across the seam (t6pp image on a Tensix tile of EACH card) ----
// T6 (tile A) --NoC--> E1(A) --link--> E1(B) --NoC--> T6 (tile B) reflects --NoC--> E1(B) --link--> E1(A) --NoC--> T6 (A) ...
// The host only configures, releases and samples.  It owns the two T6 tiles for the duration: all five cores held in reset, the t6pp image
// written at L1 0, control block at T6CTL, receive rings at --t6-addr; BRISC alone is released (reset bit 11).  Everything is restored at exit.
constexpr uint32_t T6CTL = 0x8000, T6CTL_WIN = 0x400, T6_READY = 0x7E57A11Eu, T6PP_ID = 0x76500003u, SR_B = 0x800, SR_NC = 0x40000, NC_RESET_PC = 0xFFB12238, NC_RESET_PC_OVR = 0xFFB1223C;
std::vector<uint32_t> g_t6img;
enum { P_HANDLED = 0, P_BAD = 1, P_VCHK = 2, P_NOCFAIL = 3, P_PARTIAL = 4, P_VWORD = 5, P_VGOT = 6, P_VEXP = 7, P_IDX = 8 };
struct PPTile { Side* s = nullptr; unsigned x = 1, y = 2; uint32_t sr = 0xFFFFFFFFu; bool touched = false; bool ncregs = false; uint32_t pc0 = 0, pco0 = 0; };
unsigned g_hart_nc = 0, g_nribuf = 3;   // --t6-hart nc, --nri-buf
unsigned g_t6e1x = 0xFFu;                // --t6-e1x N: explicit ETH node X for the T6 image (default: NoC0 x, or 16-x with --t6-noc 1)
unsigned g_t6noc = 0;                    // --t6-noc 0|1: NIU the T6 image drives for its writes to the ETH tile (t6pp3)
bool g_tile_unavail = false;   // a requested tile is harvested/unusable (tool exits with code 4, not a hardware fault)
uint32_t g_rbase = 0x100000, g_rspan = 0x4100;

inline uint32_t PPR(PPTile& t, uint32_t a) { uint32_t v = 0; if (!t.s->p->rd32y(t.x, t.y, a, v)) v = 0xDEADDEAD; return v; }
bool pp_wr_ok(uint32_t a) {
  if (a & 3u) return false;
  if (a < (uint32_t)g_t6img.size() * 4u) return true;                   // image at L1 0
  if (a >= T6CTL && a < T6CTL + T6CTL_WIN) return true;                 // control block
  return a >= g_rbase && a < g_rbase + g_rspan;                         // receive rings
}
void PPW(PPTile& t, uint32_t a, uint32_t v) {
  if (!pp_wr_ok(a)) { fprintf(stderr, "INTERNAL: T6 write 0x%x refused\n", a); exit(3); }
  if (!g_go) return;
  if (!t.s->p->wr32y(t.x, t.y, a, v)) ++g_bad;
}
bool pp_sr(PPTile& t, uint32_t v) {      // allowlisted: {orig, orig|ALL (all five cores in reset), (orig|ALL) with only BRISC (bit 11) or only NCRISC (bit 18) released}
  if (t.sr == 0xFFFFFFFFu || (v != t.sr && v != (t.sr | T6_ALL_RESET) && v != ((t.sr | T6_ALL_RESET) & ~SR_B) && v != ((t.sr | T6_ALL_RESET) & ~SR_NC))) { fprintf(stderr, "INTERNAL: T6 soft-reset write 0x%x refused\n", v); exit(3); }
  if (!g_go) return true;
  uint32_t b = 0;
  if (!t.s->p->wr32y(t.x, t.y, T6_SOFT_RESET, v) || !t.s->p->rd32y(t.x, t.y, T6_SOFT_RESET, b) || b != v) { printf("  T6 (%u,%u) soft-reset write/readback failed (wrote 0x%x read 0x%x)\n", t.x, t.y, v, b); ++g_bad; return false; }
  return true;
}
bool pp_ncregs(PPTile& t, bool set) {   // NCRISC start address: only these two registers, only {16 / 1} or the values read at prepare
  if (!g_go) return true;
  uint32_t pc = set ? 16u : t.pc0, po = set ? 1u : t.pco0;
  if (set && !t.ncregs) { t.s->p->rd32y(t.x, t.y, NC_RESET_PC, t.pc0); t.s->p->rd32y(t.x, t.y, NC_RESET_PC_OVR, t.pco0); t.ncregs = true; printf("  T6 (%u,%u): NCRISC reset-PC regs were 0x%x / 0x%x; setting 16 / 1 (restored at exit)\n", t.x, t.y, t.pc0, t.pco0); pc = 16u; po = 1u; }
  if (!set && !t.ncregs) return true;
  uint32_t b1 = 0, b2 = 0;
  if (!t.s->p->wr32y(t.x, t.y, NC_RESET_PC, pc) || !t.s->p->wr32y(t.x, t.y, NC_RESET_PC_OVR, po) ||
      !t.s->p->rd32y(t.x, t.y, NC_RESET_PC, b1) || !t.s->p->rd32y(t.x, t.y, NC_RESET_PC_OVR, b2)) { ++g_bad; return false; }
  if (b1 != pc || b2 != po) printf("  T6 (%u,%u): NCRISC reset-PC readback 0x%x/0x%x (wrote 0x%x/0x%x) -- registers may not read back\n", t.x, t.y, b1, b2, pc, po);
  if (!set) t.ncregs = false;
  return true;
}
bool pp_prepare(PPTile& t, const char* nm, uint32_t nch) {
  uint32_t ep = 0, cfg = 0, sr = 0;
  t.s->p->rd32y(t.x, t.y, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, ep); t.s->p->rd32y(t.x, t.y, NIU0_ADDR + NIU_CFG_0_OFF, cfg); t.s->p->rd32y(t.x, t.y, T6_SOFT_RESET, sr);
  t.sr = sr;
  printf("  T6 %s (NoC0 %u,%u) on card %d: NIU endpoint id 0x%08x, NIU cfg 0x%08x%s, soft-reset 0x%08x (cores %s)\n", nm, t.x, t.y, t.s->dev, ep, cfg,
         (cfg & NIU_CFG_0_HARVESTED) ? " HARVESTED" : "", sr, (sr & T6_ALL_RESET) == T6_ALL_RESET ? "all in reset" : "NOT all in reset");
  if (cfg & NIU_CFG_0_HARVESTED) { printf("  tile %s (%u,%u) is harvested: SKIPPING this tile pair (exit code 4)\n", nm, t.x, t.y); g_tile_unavail = true; return false; }
  if (!g_go) { printf("  (dry-run) would hold the tile's five cores in reset, write the %zu-byte t6pp image at L1 0, zero the control block and %u receive rings @0x%x\n", g_t6img.size() * 4, nch, g_rbase); return true; }
  t.touched = true;
  if (!pp_sr(t, sr | T6_ALL_RESET)) return false;
  for (size_t i = 0; i < g_t6img.size(); ++i) PPW(t, 4 * (uint32_t)i, g_t6img[i]);
  unsigned bad = 0;
  for (size_t i = 0; i < g_t6img.size(); ++i) bad += (PPR(t, 4 * (uint32_t)i) != g_t6img[i]);
  if (bad) { printf("  T6 %s image readback mismatch in %u words; aborting\n", nm, bad); return false; }
  for (uint32_t a = T6CTL; a < T6CTL + T6CTL_WIN; a += 4) PPW(t, a, 0);
  for (uint32_t a = g_rbase; a < g_rbase + g_rspan; a += 4) PPW(t, a, 0);
  for (uint32_t a = g_rbase; a < g_rbase + g_rspan; a += 4) if (PPR(t, a) != 0) { printf("  T6 %s ring zero readback failed at 0x%x\n", nm, a); return false; }
  return true;
}
void pp_restore(PPTile& t) {
  if (!t.touched || !g_go) return;
  (void)pp_sr(t, t.sr | T6_ALL_RESET);       // cores held first, then the reset-PC registers, then the original soft-reset value
  (void)pp_ncregs(t, false);
  if (pp_sr(t, t.sr)) printf("  T6 (%u,%u) on card %d soft-reset restored to 0x%08x\n", t.x, t.y, t.s->dev, t.sr);
  t.touched = false;
}
inline uint32_t pps(PPTile& t, uint32_t c, uint32_t k) { return PPR(t, T6CTL + 0x100 + c * 0x40 + 4 * k); }

// ETH node X as the T6 image must address it: NoC0 raw x over NIU0; over NIU1 the Y=1 row is untranslated and raw X is mirrored (16 - x).
constexpr unsigned NOC_RAW_MAXX = 16;
inline unsigned t6_e1x(unsigned ethx) {
  if (g_t6e1x != 0xFFu) return g_t6e1x;
  return g_t6noc == 1 ? NOC_RAW_MAXX - ethx : ethx;
}
// configure + release one tile's BRISC.  role 0 = initiator (seeds), 1 = reflector.
bool pp_release(PPTile& t, const char* nm, uint32_t role, uint32_t nch, uint32_t K, uint32_t ve, uint32_t tcms, unsigned ethx) {
  if (!pp_sr(t, t.sr | T6_ALL_RESET)) return false;                     // (re)hold everything, then restart BRISC from address 0
  for (uint32_t a = T6CTL; a < T6CTL + T6CTL_WIN; a += 4) PPW(t, a, 0);
  for (uint32_t c = 0; c < nch; ++c) for (uint32_t i = 0; i < RING_CARS; ++i) PPW(t, g_rbase + c * RING_BYTES + i * CAR, 0);
  PPW(t, T6CTL + 0x10, role); PPW(t, T6CTL + 0x14, nch); PPW(t, T6CTL + 0x18, K); PPW(t, T6CTL + 0x1c, ve); PPW(t, T6CTL + 0x20, tcms);
  PPW(t, T6CTL + 0x24, (1u << 6) | t6_e1x(ethx)); PPW(t, T6CTL + 0x28, (t.y << 6) | t.x);
  PPW(t, T6CTL + 0x48, g_nribuf == 0 ? 0xFFu : g_nribuf);
  PPW(t, T6CTL + 0x4c, g_t6noc);
  PPW(t, T6CTL + 0x2c, OUT0); PPW(t, T6CTL + 0x30, RING_BYTES); PPW(t, T6CTL + 0x34, g_rbase); PPW(t, T6CTL + 0x38, RING_BYTES);
  (void)PPR(t, T6CTL);
  if (g_hart_nc && !pp_ncregs(t, true)) return false;
  if (!pp_sr(t, (t.sr | T6_ALL_RESET) & ~(g_hart_nc ? SR_NC : SR_B))) return false;           // release ONE hart: BRISC (bit 11) or NCRISC (bit 18)
  for (int i = 0; i < 2000; ++i) {
    if (PPR(t, T6CTL + 8) == T6_READY) {
      uint32_t id = PPR(t, T6CTL + 0x3c);
      if (id != T6PP_ID) { printf("  T6 %s ready but image id 0x%08x != 0x%08x\n", nm, id, T6PP_ID); return false; }
      return true;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  printf("  T6 %s (%u,%u) did NOT come up (no ready word)\n", nm, t.x, t.y);
  return false;
}
bool pp_stop(PPTile& t) {
  PPW(t, T6CTL + 4, 1);
  bool done = false;
  for (int i = 0; i < 20000 && !done; ++i) { done = PPR(t, T6CTL + 0xc) == 1; if (!done) std::this_thread::sleep_for(std::chrono::microseconds(50)); }
  PPW(t, T6CTL + 0, 0);
  return done;
}

bool test_pp(Side& A, Side& B, PPTile& ta, PPTile& tb, const std::vector<uint32_t>& sizes, const std::vector<uint32_t>& ks, uint32_t nch, unsigned dur_ms, uint32_t ve, uint32_t flags) {
  printf("\n--- TEST pp: bare-metal T6 ping-pong A(%u,%u) <-> B(%u,%u) across the seam on hart %s with NIU%u cmd buffer %u, %u channel%s, %u ms per point, payload verify every %u ---\n", ta.x, ta.y, tb.x, tb.y,
         g_hart_nc ? "NC" : "B", g_t6noc, g_nribuf, nch, nch > 1 ? "s" : "", dur_ms, ve);
  printf("  T6 -> ETH node: NIU%u raw (%u,1) for ETH tile NoC0 (%u,1)%s\n", g_t6noc, t6_e1x(A.x), A.x, g_t6noc == 1 ? "  [NoC1 mirrors raw X: x1 = 16 - x0]" : "");
  if (g_t6noc == 1 && (t6_e1x(A.x) == A.x && A.x != 8)) {
    printf("  REFUSED: over NIU1 raw (%u,1) is a DIFFERENT ETH tile (the NoC1 mirror), not E1 at NoC0 (%u,1); a write there goes to a live tile.\n", A.x, A.x);
    return false;
  }
  if (g_t6noc == 1 && A.x != B.x) { printf("  REFUSED: NIU1 mirrored node tested only for the same ETH X on both cards\n"); return false; }
  bool all = true;
  uint32_t nodeA = (ta.y << 6) | ta.x, nodeB = (tb.y << 6) | tb.x;
  for (uint32_t bytes : sizes) for (uint32_t K : ks) {
    if (g_abort) return false;
    uint32_t tcms = tcms_for(bytes);
    e1_start(A, M_ROUTER, flags, nch, L_OUT | L_IN_NOC, 16, K, nodeA, g_rbase);
    e1_start(B, M_ROUTER, flags, nch, L_OUT | L_IN_NOC, 16, K, nodeB, g_rbase);
    e1_go(A); e1_go(B);
    bool up = pp_release(tb, "B(reflector)", 1, nch, K, ve, tcms, B.x) && pp_release(ta, "A(initiator)", 0, nch, K, ve, tcms, A.x);
    if (up) { PPW(tb, T6CTL + 0, 1); PPW(ta, T6CTL + 0, 1); }
    std::vector<uint32_t> a0(nch), b0(nch), a1(nch), b1(nch);
    if (up) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      for (uint32_t c = 0; c < nch; ++c) { a0[c] = pps(ta, c, P_HANDLED); b0[c] = pps(tb, c, P_HANDLED); }
    }
    auto t0 = Clock::now();
    if (up) std::this_thread::sleep_for(std::chrono::milliseconds(dur_ms));
    if (up) for (uint32_t c = 0; c < nch; ++c) { a1[c] = pps(ta, c, P_HANDLED); b1[c] = pps(tb, c, P_HANDLED); }
    double secs = us_since(t0) / 1e6;
    bool d1 = up ? pp_stop(ta) : true, d2 = up ? pp_stop(tb) : true;
    bool de = e1_stop2(A, B);
    if (!up) { printf("  size %4u B K=%u: T6 image did not start -> FAIL\n", bytes, K); all = false; break; }
    double ha = 0, hb = 0, mn = 1e300, mx = 0; uint32_t bad = 0, vc = 0, nf = 0, tf = 0, part = 0;
    for (uint32_t c = 0; c < nch; ++c) {
      double x = (double)(a1[c] - a0[c]); ha += x; hb += (double)(b1[c] - b0[c]); mn = std::min(mn, x); mx = std::max(mx, x);
      bad += pps(ta, c, P_BAD) + pps(tb, c, P_BAD); vc += pps(ta, c, P_VCHK) + pps(tb, c, P_VCHK);
      nf += pps(ta, c, P_NOCFAIL) + pps(tb, c, P_NOCFAIL) + stat(A, c, S_NOCFAIL) + stat(B, c, S_NOCFAIL);
      tf += stat(A, c, S_TXFAIL) + stat(B, c, S_TXFAIL); part += pps(ta, c, P_PARTIAL) + pps(tb, c, P_PARTIAL);
    }
    double rate = ha / secs;                                           // A's handled cars/s = round trips/s over all channels
    printf("  size %4u B K=%u x %u ch: %.3f M round trips/s total => %.2f us per car round trip (A handled %.0f, B handled %.0f in %.3f s); per-channel A min %.0f max %.0f\n",
           bytes, K, nch, rate / 1e6, rate > 0 ? (double)K * nch / rate * 1e6 : 0.0, ha, hb, secs, mn, mx);
    printf("    payload checks %u BAD %u | noc-fail %u txfail %u | partial sightings %u | stop acked: T6 A %s B %s, E1 %s", vc, bad, nf, tf, part, d1 ? "yes" : "NO", d2 ? "yes" : "NO", de ? "yes" : "NO");
    printf("\n");
    if (bad) for (uint32_t c = 0; c < nch; ++c) for (PPTile* t : {&ta, &tb})
      if (pps(*t, c, P_BAD)) printf("    ch%u first bad on T6 (%u,%u): word %u got %08x exp %08x\n", c, t->x, t->y, pps(*t, c, P_VWORD), pps(*t, c, P_VGOT), pps(*t, c, P_VEXP));
    bool ok = ha > 0 && hb > 0 && mn > 0 && !bad && !nf && !tf && d1 && d2 && de;
    printf("    -> %s\n", ok ? "OK" : "FAIL");
    all = all && ok;
    if (!ok) break;
  }
  return all;
}

// ---- read-only diagnostics ----
struct Health { uint32_t port, hb, retrain, fecc, fecu, dyn[16]; };
Health health_snap(Side& s) {
  Health h{};
  h.port = R(s, ETH_BOOT_RESULTS_ADDR + 4); h.hb = R(s, 0x7CC70); h.retrain = R(s, 0x7CE00); h.fecc = R(s, 0x7CE90); h.fecu = R(s, 0x7CE98);
  for (int i = 0; i < 16; ++i) h.dyn[i] = R(s, 0x7D040 + 4 * i);
  return h;
}
void health_print(const char* label, Side& s, const Health& cur, const Health* base) {
  printf("  %s %s dev%d: port %u  heartbeat 0x%08x  retrain %u  FEC corrected %u uncorrected %u  words at 0x7D040 (semantics unverified):", label, s.name.c_str(), s.dev, cur.port, cur.hb, cur.retrain, cur.fecc, cur.fecu);
  for (int i = 0; i < 16; ++i) printf(" %x", cur.dyn[i]);
  printf("\n");
  if (base) {
    bool dyn = false; for (int i = 0; i < 16; ++i) dyn = dyn || cur.dyn[i] != base->dyn[i];
    printf("    delta over the run (after - before): heartbeat %s  retrain %+d  FEC corrected %+d uncorrected %+d  port %u->%u  0x7D040 words %s\n", cur.hb != base->hb ? "advanced" : "unchanged (heartbeat semantics unverified)",
           (int)(cur.retrain - base->retrain), (int)(cur.fecc - base->fecc), (int)(cur.fecu - base->fecu), base->port, cur.port, dyn ? "CHANGED" : "unchanged");
  }
}
void dump_niu(Side& s) {
  static const uint32_t offs[] = {0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x40};
  printf("  %s dev%d X=%u: NIU command-buffer registers (offsets 00 04 08 0c 10 14 18 1c 20 24 40)\n", s.name.c_str(), s.dev, s.x);
  for (unsigned noc = 0; noc < 2; ++noc) for (unsigned b = 0; b < 4; ++b) {
    printf("    NIU%u buf%u:", noc, b);
    bool any = false;
    for (uint32_t o : offs) { uint32_t v = R(s, (noc ? 0xFFB30000u : NIU0_ADDR) + b * 0x800u + o); any = any || v; printf(" %08x", v); }
    printf("%s\n", any ? "" : "   (all zero)");
  }
}

void usage() {
  fprintf(stderr,
          "ethfwd20 -- eth-seam multi-channel tests for the e1fwd13/e1fwd14 router image (cards A and B both run E1; host plays the hubs)\n"
          "  --link A:B:X          cards A and B, ETH tile NoC0 X (e.g. 0:1:6)\n"
          "  --image FILE.bin      REQUIRED: flat E1 binary built by e1fwd/build.sh (e1fwd13 or e1fwd14), linked at 0x4000, <= 0x4000 B\n"
          "  --go                  actually write (default: dry run, reads only)\n"
          "  --idle --route --reflect   select tests (default: all three)\n"
          "  --nch N               channels used by route/reflect (1..8, default 2); idle sweeps 1 and N channels\n"
          "  --sizes a,b,c         car sizes in bytes: 16, 32 or a multiple of 64 up to 4160 (default 64,576,4160)\n"
          "  --rounds N            route rounds per size (default 500; each round = 4 cars per direction per channel)\n"
          "  --ks 1,2,4            reflect cars in flight per channel (default 1,4)\n"
          "  --dur-ms N            reflect/idle measurement time per point (default 1000)\n"
          "  --regen               reflect: E1 regenerates and periodically verifies payload each hop\n"
          "  --verify-every N      with --regen: verify every Nth car (default 16)\n"
          "  --noc                 NoC-leg test (not default): E1 on card B NoC-writes cars into a T6 tile's L1; needs --t6/--t6-addr below\n"
          "  --t6 X:Y              --noc: Tensix tile, NoC0 coordinates, ON CARD B (default 1:2).  The tool puts its RISC-V cores in reset\n"
          "                        (restored at exit), zeroes the region and reads it back.  PICK A TILE/REGION NOTHING USES\n"
          "  --t6-addr ADDR        --noc: 16B-aligned L1 address of the nch*0x4100-byte region (default 0x100000; range 0x20000..0x160000)\n"
          "  --vcstatic            --noc: E1 sets NOC_CMD_VC_STATIC in its NoC writes (e1fwd15 image)\n"
          "  --pp                  stage 3a (not default): bare-metal T6 ping-pong across the seam; needs --t6-image, optional --t6a/--t6b/--ppve\n"
          "  --t6-image FILE.bin   the t6pp image (sh t6pp/build.sh), linked at L1 0, <= 0x4000 B\n"
          "  --t6a X:Y --t6b X:Y   --pp: Tensix tiles (NoC0 coordinates) on card A / card B (default 1:2 each); harvested tile => exit code 4\n"
          "  --txqfix              E1 does the BH-55 workaround: dummy TXQ CMD read before each STATUS poll (e1fwd16 image)\n"
          "  --e1-noc 0|1          E1's NoC writes over NIU0 (default) or NIU1 (e1fwd16; NoC1 from an ETH tile is UNPROVEN)\n"
          "  --e1-nri-buf N        NIU command buffer the E1 drives for NoC writes (0..3, default 3; e1fwd16)\n"
          "  --dump-niu-after      READ-ONLY: also dump the NIU command-buffer registers at the end of the run\n"
          "  --dump-niu            READ-ONLY: dump NIU0/NIU1 command-buffer registers of the ETH tile on both cards, then exit\n"
          "  --t6-hart b|nc        --pp: start the image on BRISC (default, reset address 0) or NCRISC (reset PC 16, as MFMx does)\n"
          "  --nri-buf N           --pp: NIU command buffer the T6 image drives (0..3, default 3 = NRI3)\n"
          "  --t6-e1x N            --pp: ETH node X the T6 image writes to (default NoC0 x; with --t6-noc 1: 16 - x, the NoC1 raw mirror). Unflipped over NIU1 is refused\n"
          "  --t6-noc 0|1          --pp: the T6 image's writes to the ETH tile use NIU0 (default) or NIU1 (t6pp3; T6 -> ETH over NoC1 is UNPROVEN)\n"
          "  --ppve N              --pp: T6 verifies payload of every Nth car (default 16, 0 = never)\n"
          "  --csr                 E1 sets DisLowCache (CSR 0x7c0 bit3) and polls without fence (proven on E1 hw 2026-11-16)\n"
          "  --keep                leave queue 2 / TX header 1 configured\n"
          "  --force               proceed even if queue 2 is not pristine or E1 is not in reset\n"
          "  --verbose             print every write / per-channel counters\n"
          "  --mock                logic test against a FAKE model (all E1 results are FAKE)\n");
}

}  // namespace

int main(int argc, char** argv) {
  const char* imagePath = nullptr;
  bool keep = false, force = false, mock = false, haveLink = false, regen = false, csr = false, tIdle = false, tRoute = false, tReflect = false, tNoc = false, vcs = false, tPP = false, txqfix = false, e1noc = false, dumpniu = false, dumpafter = false;
  const char* t6imgPath = nullptr; unsigned ppve = 16; PPTile pta, ptb;
  LinkSpec ls{0, 0, 0};
  uint32_t rounds = 500, ve = 16, nch = 2; unsigned durMs = 1000;
  std::vector<uint32_t> sizes = {64, 576, 4160}, ks = {1, 4};
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i]; unsigned v = 0, q = 0, r = 0;
    if (a == "--link" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u:%u", &v, &q, &r) == 3) { ls = {(int)v, (int)q, r}; haveLink = true; ++i; }
    else if (a == "--sizes" && i + 1 < argc) sizes = parse_list(argv[++i]);
    else if (a == "--ks" && i + 1 < argc) ks = parse_list(argv[++i]);
    else if (a == "--rounds" && i + 1 < argc && parse_u(argv[i + 1], v)) { rounds = v; ++i; }
    else if (a == "--nch" && i + 1 < argc && parse_u(argv[i + 1], v)) { nch = v; ++i; }
    else if (a == "--dur-ms" && i + 1 < argc && parse_u(argv[i + 1], v)) { durMs = v; ++i; }
    else if (a == "--verify-every" && i + 1 < argc && parse_u(argv[i + 1], v)) { ve = v; ++i; }
    else if (a == "--image" && i + 1 < argc) imagePath = argv[++i];
    else if (a == "--go") g_go = true;
    else if (a == "--idle") tIdle = true;
    else if (a == "--route") tRoute = true;
    else if (a == "--reflect") tReflect = true;
    else if (a == "--noc") tNoc = true;
    else if (a == "--txqfix") txqfix = true;
    else if (a == "--e1-noc" && i + 1 < argc && parse_u(argv[i + 1], v) && v <= 1) { e1noc = v == 1; ++i; }
    else if (a == "--e1-nri-buf" && i + 1 < argc && parse_u(argv[i + 1], v) && v <= 3) { g_e1buf = v; ++i; }
    else if (a == "--dump-niu") dumpniu = true;
    else if (a == "--dump-niu-after") dumpafter = true;
    else if (a == "--pp") tPP = true;
    else if (a == "--t6-image" && i + 1 < argc) t6imgPath = argv[++i];
    else if (a == "--t6a" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) { pta.x = v; pta.y = q; ++i; }
    else if (a == "--t6b" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) { ptb.x = v; ptb.y = q; ++i; }
    else if (a == "--t6-hart" && i + 1 < argc) { std::string h = argv[++i]; if (h == "nc") g_hart_nc = 1; else if (h == "b") g_hart_nc = 0; else { fprintf(stderr, "bad --t6-hart %s (b or nc)\n", h.c_str()); return 2; } }
    else if (a == "--nri-buf" && i + 1 < argc && parse_u(argv[i + 1], v) && v <= 3) { g_nribuf = v; ++i; }
    else if (a == "--t6-e1x" && i + 1 < argc && parse_u(argv[i + 1], v) && v <= NOC_RAW_MAXX) { g_t6e1x = v; ++i; }
    else if (a == "--t6-noc" && i + 1 < argc && parse_u(argv[i + 1], v) && v <= 1) { g_t6noc = v; ++i; }
    else if (a == "--ppve" && i + 1 < argc && parse_u(argv[i + 1], v)) { ppve = v; ++i; }
    else if (a == "--vcstatic") vcs = true;
    else if (a == "--t6" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) { g_t6.x = v; g_t6.y = q; ++i; }
    else if (a == "--t6-addr" && i + 1 < argc && parse_u(argv[i + 1], v)) { g_t6.base = v; ++i; }
    else if (a == "--regen") regen = true;
    else if (a == "--csr") csr = true;
    else if (a == "--keep") keep = true;
    else if (a == "--force") force = true;
    else if (a == "--verbose") g_verbose = true;
    else if (a == "--mock") mock = true;
    else { fprintf(stderr, "unknown or malformed argument: %s%s%s\n\n", a.c_str(), i + 1 < argc ? " (next: " : "", i + 1 < argc ? argv[i + 1] : ""); usage(); return 2; }
  }
  if (!tIdle && !tRoute && !tReflect && !tNoc && !tPP) tIdle = tRoute = tReflect = true;
  if (!haveLink || ls.a == ls.b || (!imagePath && !dumpniu)) { usage(); return 2; }
  if (rounds == 0 || ve == 0 || durMs == 0 || nch < 1 || nch > NCH_MAX) { fprintf(stderr, "bad --rounds/--verify-every/--dur-ms/--nch\n"); return 2; }
  for (uint32_t s : sizes) if (!size_ok(s)) { fprintf(stderr, "bad size %u (16, 32, or multiple of 64 up to 4160)\n", s); return 2; }
  for (uint32_t k : ks) if (k < 1 || k > RING_CARS) { fprintf(stderr, "bad K %u (1..%u)\n", k, RING_CARS); return 2; }
  g_t6.span = nch * RING_BYTES; g_t6.on = tNoc;
  if (tNoc && (g_t6.base % 16 || g_t6.base < 0x20000 || g_t6.base + g_t6.span > 0x160000 || g_t6.y < 2 || g_t6.y > 11 || !((g_t6.x >= 1 && g_t6.x <= 7) || (g_t6.x >= 10 && g_t6.x <= 16)))) {
    fprintf(stderr, "bad --t6/--t6-addr (need 16B-aligned base with base+nch*0x4100 within 0x20000..0x160000, x 1..16, y>=2)\n"); return 2; }
  g_rbase = g_t6.base; g_rspan = nch * RING_BYTES;
  if (tPP) {
    if (!t6imgPath) { fprintf(stderr, "--pp needs --t6-image FILE.bin (sh t6pp/build.sh)\n"); return 2; }
    for (PPTile* t : {&pta, &ptb})
      if (!(t->x >= 1 && t->x <= 7) && !(t->x >= 10 && t->x <= 16)) { fprintf(stderr, "bad --t6a/--t6b tile %u,%u: Tensix columns are x 1..7 and 10..16 (8,9 are other tile types)\n", t->x, t->y); return 2; }
      else if (t->y < 2 || t->y > 11) { fprintf(stderr, "bad --t6a/--t6b tile %u,%u: Tensix rows are y 2..11\n", t->x, t->y); return 2; }
    if (g_rbase % 16 || g_rbase < 0x20000 || g_rbase + g_rspan > 0x160000) { fprintf(stderr, "bad --t6-addr for --pp (need 16B-aligned, base+nch*0x4100 within 0x20000..0x160000)\n"); return 2; }
    if (ppve > 1000000) { fprintf(stderr, "bad --ppve\n"); return 2; }
    FILE* f = fopen(t6imgPath, "rb");
    if (!f) { fprintf(stderr, "cannot open --t6-image %s\n", t6imgPath); return 2; }
    std::vector<unsigned char> b; unsigned char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    if (b.empty() || b.size() % 4 || b.size() > 0x4000) { fprintf(stderr, "bad --t6-image: %zu bytes (need a nonzero multiple of 4, at most 0x4000)\n", b.size()); return 2; }
    g_t6img.resize(b.size() / 4);
    for (size_t i = 0; i < g_t6img.size(); ++i) g_t6img[i] = (uint32_t)b[4*i] | (uint32_t)b[4*i+1] << 8 | (uint32_t)b[4*i+2] << 16 | (uint32_t)b[4*i+3] << 24;
    printf("T6 image: %s (%zu bytes)\n", t6imgPath, b.size());
  }
  g_verbose = g_verbose || !g_go;
  if (imagePath) {
    FILE* f = fopen(imagePath, "rb");
    if (!f) { fprintf(stderr, "cannot open --image %s\n", imagePath); return 2; }
    std::vector<unsigned char> b; unsigned char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    if (b.empty() || b.size() % 4 || b.size() > 0x4000) { fprintf(stderr, "bad --image: %zu bytes (need a nonzero multiple of 4, at most 0x4000)\n", b.size()); return 2; }
    g_img.resize(b.size() / 4);
    for (size_t i = 0; i < g_img.size(); ++i) g_img[i] = (uint32_t)b[4*i] | (uint32_t)b[4*i+1] << 8 | (uint32_t)b[4*i+2] << 16 | (uint32_t)b[4*i+3] << 24;
    printf("E1 image: %s (%zu bytes)\n", imagePath, b.size());
  }

  Fabric fab; std::map<int, std::unique_ptr<Port>> ports;
  auto port_for = [&](int d) -> Port* {
    auto it = ports.find(d); if (it != ports.end()) return it->second.get();
    if (mock) { ports[d].reset(new MockPort(fab, d)); return ports[d].get(); }
    HwPort* hp = new HwPort(); std::string err; ports[d].reset(hp);
    if (!hp->open_dev("/dev/tenstorrent/" + std::to_string(d), err)) { fprintf(stderr, "dev%d: %s\n", d, err.c_str()); ports.erase(d); return nullptr; }
    return hp;
  };
  if (mock) { printf("*** MOCK MODE: FAKE model; E1 results below are FAKE ***\n"); fab.init(ls.x); }
  printf("%s\n", g_go ? "MODE: --go (WILL WRITE)" : "MODE: dry run (reads only; writes are printed, not issued)");
  Port* pa = port_for(ls.a); Port* pb = port_for(ls.b);
  if (!pa || !pb) return 1;
  Side A{pa, ls.a, ls.x, "A"}, B{pb, ls.b, ls.x, "B"};
  printf("--- validate ---\n");
  if (!validate(A, force) || !validate(B, force)) return 1;
  if (A.pmajor != B.major || A.pminor != B.minor || B.pmajor != A.major || B.pminor != A.minor) { printf("link check FAILED: not FW-reported peers\n"); return 1; }
  printf("link check OK\n");
  Health hA0 = health_snap(A), hB0 = health_snap(B);
  printf("\n--- link health (read-only) ---\n");
  health_print("before:", A, hA0, nullptr); health_print("before:", B, hB0, nullptr);
  if (dumpniu) { printf("\n--- NIU command-buffer dump (read-only) ---\n"); dump_niu(A); dump_niu(B); printf("\nDONE (read-only)\n"); return 0; }
  if (mock) printf("(mock note: the fake fabric only models x-seam pairs 0<->1 and 2<->3)\n");

  signal(SIGINT, on_sigint);
  printf("\n--- configure queue 2 on both ends ---\n");
  for (Side* s : {&A, &B}) { W(*s, RXQ2 + 0x0c, RXBUF_L1 >> 4); W(*s, RXQ2 + 0x10, 0x100); W(*s, RXQ2 + 0x18, 0xe); W(*s, RXQ2 + 0x00, 2); }
  for (Side* s : {&A, &B}) {
    Side* o = (s == &A) ? &B : &A; uint32_t sa0, sa1, da0, da1;
    mac_words(s->major, s->minor, sa0, sa1); mac_words(o->major, o->minor, da0, da1);
    W(*s, TXHDR1 + 0x10, sa0); W(*s, TXHDR1 + 0x14, sa1); W(*s, TXHDR1 + 0x18, da0); W(*s, TXHDR1 + 0x1c, da1); W(*s, TXQ2 + 0x84, 1);
  }
  for (Side* s : {&A, &B}) W(*s, TXQ2 + 0x00, 1);
  if (g_go) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  for (Side* s : {&A, &B}) W(*s, TXQ2 + 0x80, 1u << 8);      // header entry 1 must be selected before ANY send

  bool pass = true; E1State eA, eB;
  if (g_go) {
    printf("\n--- smoke (64 B tagged, both directions) ---\n");
    uint32_t g1 = tagged_window(A, B, 64, 1, 1, nullptr), g2 = tagged_window(B, A, 64, 1, 2, nullptr);
    printf("  A->B %s, B->A %s\n", g1 == 1 ? "OK" : "FAIL", g2 == 1 ? "OK" : "FAIL"); pass = g1 == 1 && g2 == 1;
  }
  printf("\n--- E1 deploy ---\n");
  bool up = pass && e1_deploy(B, eB, force);
  if (up) up = e1_deploy(A, eA, force);
  if (up && g_go) {
    uint32_t ia = R(A, CTL + 0x54), ib = R(B, CTL + 0x54);
    auto nm = [](uint32_t id) { return id == ID_E1FWD13 ? "e1fwd13" : id == ID_E1FWD14 ? "e1fwd14" : id == ID_E1FWD15 ? "e1fwd15" : id == ID_E1FWD16 ? "e1fwd16" : "UNKNOWN"; };
    printf("  image id: A 0x%08x (%s)  B 0x%08x (%s)\n", ia, nm(ia), ib, nm(ib));
    if (ia != ib || (ia != ID_E1FWD13 && ia != ID_E1FWD14 && ia != ID_E1FWD15 && ia != ID_E1FWD16)) { printf("  not an e1fwd13..16-layout image; refusing to run tests\n"); up = false; }
  }
  if (!g_go) {
    if (tNoc) { printf("\n--- NoC test T6 prepare (dry run) ---\n"); t6_prepare(B); }
    if (tPP) { pta.s = &A; ptb.s = &B; printf("\n--- pp T6 prepare (dry run) ---\n"); pp_prepare(pta, "A", nch); pp_prepare(ptb, "B", nch); }
    printf("\nDry run: would run tests%s%s%s%s on %u channel(s).  Re-run with --go.\n", tIdle ? " idle" : "", tRoute ? " route" : "", tReflect ? " reflect" : "", tNoc ? " noc" : "", nch);
    return g_tile_unavail ? 4 : 0;
  }
  uint32_t fl = (csr ? F_CSR : F_FENCE) | (vcs ? F_VCSTATIC : 0) | (txqfix ? F_TXQFIX : 0) | (e1noc ? F_NOC1 : 0);
  if (txqfix || e1noc || g_e1buf != 3) printf("E1 experiment options: txqfix=%d e1-noc=%d e1-nri-buf=%u (need the e1fwd16 image; older images ignore them)\n", (int)txqfix, (int)e1noc, g_e1buf);
  if (up) {
    if (tIdle && pass) { std::vector<uint32_t> n = {1}; if (nch > 1) n.push_back(nch); if (!test_idle(A, B, n, durMs, fl)) pass = false; }
    if (tRoute && pass) { if (!test_route(A, B, sizes, nch, rounds, fl)) pass = false; }
    if (tReflect && pass) { if (!test_reflect(A, B, sizes, ks, nch, durMs, regen, ve, fl)) pass = false; }
    if (tNoc && pass) {
      printf("\n--- NoC test T6 prepare ---\n");
      if (!t6_prepare(B)) pass = false;
      if (pass && !test_noc(A, B, sizes, nch, rounds, fl)) pass = false;
      t6_restore(B);
    }
    if (tPP && pass) {
      pta.s = &A; ptb.s = &B;
      printf("\n--- pp T6 prepare ---\n");
      bool pr = pp_prepare(pta, "A", nch) && pp_prepare(ptb, "B", nch);
      if (!pr) pass = false;
      if (pr && !test_pp(A, B, pta, ptb, sizes, ks, nch, durMs, ppve, fl)) pass = false;
      pp_restore(pta); pp_restore(ptb);
    }
  } else pass = false;

  e1_reset(eA); e1_reset(eB);
  { Health hA1 = health_snap(A), hB1 = health_snap(B);
    printf("\n--- link health after the run (read-only) ---\n");
    health_print("after: ", A, hA1, &hA0); health_print("after: ", B, hB1, &hB0); }
  if (dumpafter) { printf("\n--- NIU command-buffer dump after the run (read-only) ---\n"); dump_niu(A); dump_niu(B); }
  printf("\n--- final queue-2 state ---\n");
  for (Side* s : {&A, &B}) printf("  %s TXQ2 ctrl=%x xfer=%u | RXQ2 ctrl=%x seq=%u rseq=%u drops=%u\n", s->name.c_str(), R(*s, TXQ2), R(*s, TXQ2 + 0x30), R(*s, RXQ2), R(*s, RXQ2 + 0x40), R(*s, RXQ2 + 0x44), R(*s, RXQ2 + 0x4c));
  if (!keep) { printf("\n--- restore ---\n"); restore(A); restore(B); }
  printf("\nRESULT: %s (%d write/readback problems)\n", (pass && !g_bad) ? "PASS" : "FAIL/INCONCLUSIVE", g_bad);
  if (g_tile_unavail) return 4;
  return (pass && !g_bad) ? 0 : 1;
}
