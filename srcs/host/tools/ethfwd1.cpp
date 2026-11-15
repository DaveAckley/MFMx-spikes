// ethfwd1.cpp -- eth-seam stages 0 and 1: E1 (ERISC) car forwarder between two Blackhole cards over one QSFP-DD link.
//
// STAGE 0 (--stage0): L0 data-cache check on the ETH E1 core.  The host writes an incrementing counter to one L1
//   word as fast as it can while E1 polls it in three modes: no fence, fence, and "no fence after setting DisLowCache
//   (CSR 0x7c0 bit 3, as MFMx cross/src/_BUD.S does for the T6 harts)".  Reports values missed and ns/poll.  If the
//   CSR does not exist on E1 the core may trap/hang: the tool detects the missing stop-ack, resets E1 and carries on.
// STAGE 1a (--stage1a): functional test of E1 car forwarding with the HOST acting as the hubs.  Both cards run E1 in
//   FWD role.  The host fills OUT-ring slots (MFMx cars, 4 slots, FIFO, header written last) on one card; E1 polls the
//   ring, sends each complete car over TT-link queue 2 (<= 1456 B chunks) into the same-index IN slot of the peer; the
//   host reads the peer's IN ring over PCIe and verifies every word.  Both directions at once, sizes up to 4160 B.
// STAGE 1b (--stage1b): autonomous throughput.  Both E1s run REFLECT role: each bounces any new complete car it finds
//   in its IN ring straight back (nonce+1, state flipped) so K cars ping-pong over the link with no host in the loop.
//   The host only seeds K cars and samples the E1 hop counters.  Reports hops/s, ns/hop, MB/s per size and K.
//
// All car rules follow shared/include/TCMarker.h / TCBase.h (header word 0x2C|nonce<<8|tcms<<16|state<<24; header ==
// ankle (tcms>=2) == footer => complete).  A car is "owned" by the far end after shipping; the car exchange itself is
// the flow control, so E1 adds no credits.  The E1 image is e1fwd/e1fwd1.c (embedded below; rebuild with e1fwd/build.sh).
//
// SAFETY: default is a DRY RUN (reads only).  --go enables writes.  Writes are allowlisted (write_ok): queue 2, TX header
// entry 1, L1 scratch 0x10000..0x6FFFF, E1 code 0x4000..0x4FFF, E1 control/stat block 0x8000..0x83FF, E1 PC regs, and the
// soft-reset register restricted to {original, original with only the E1 bit cleared}.  E1 itself writes only its stat
// block, car rings, and TXQ2 SRC/SIZE/DEST/CMD.  tt-kmd NOC_CLEANUP is armed per card so E1 returns to reset if this
// process dies (E1 does NOT survive this process: the cleanup fires when the fd closes).
//
// CONFIDENCE: nothing here has run on hardware.  Logic of the E1 image is verified natively (e1fwd/hosttest.c, simulated
// TXQ2 + two cores); the host tool is verified only against --mock.  Unknowns: CSR 0x7c0 on E1, behaviour with 4160 B
// cars as 3 chunks, E1 loop speed.
//
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd1.cpp -o ethfwd1
// Run:   ./ethfwd1 --link 0:1:6                         # dry run
//        ./ethfwd1 --link 0:1:6 --go                    # all stages
//        ./ethfwd1 --link 0:1:6 --go --stage0           # just the L0 test
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
};

// One fd per card; one 2 MiB TLB window per (tile, 2MiB-base) actually used, so alternating between
// tiles/links costs no ioctl.
struct HwPort : Port {
  struct Win { unsigned x; uint64_t base; uint32_t id; volatile uint8_t* p; };
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
  volatile uint32_t* map(unsigned x, uint32_t a) {
    if (a & 3u) return nullptr;
    uint64_t base = (uint64_t)a & ~TLB_MASK; uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    for (Win& w : wins) if (w.x == x && w.base == base) return (volatile uint32_t*)(w.p + off);
    if (wins.size() >= 32) return nullptr;
    tt_allocate_tlb at; memset(&at, 0, sizeof at); at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) { fprintf(stderr, "ALLOCATE_TLB: %s\n", strerror(errno)); return nullptr; }
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) { fprintf(stderr, "mmap: %s\n", strerror(errno)); return nullptr; }
    tt_configure_tlb c; memset(&c, 0, sizeof c);
    c.in.id = at.out.id; c.in.config.addr = base;
    c.in.config.x_start = x; c.in.config.x_end = x; c.in.config.y_start = 1; c.in.config.y_end = 1;
    c.in.config.noc = 0; c.in.config.mcast = 0;
    if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) { fprintf(stderr, "CONFIGURE_TLB: %s\n", strerror(errno)); return nullptr; }
    wins.push_back({x, base, at.out.id, (volatile uint8_t*)p});
    return (volatile uint32_t*)((volatile uint8_t*)p + off);
  }
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override { volatile uint32_t* p = map(x, a); if (!p) return false; v = *p; return true; }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override { volatile uint32_t* p = map(x, a); if (!p) return false; *p = v; return true; }
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
  static uint64_t key(int d, unsigned x, uint32_t a) { return ((uint64_t)d << 48) | ((uint64_t)x << 40) | a; }
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
constexpr uint32_t E1_CODE = 0x4000, CTL = 0x8000, SOFT_RESET = 0xFFB121B0, E1_PC = 0xFFB14008, E1_END = 0xFFB1400C;
constexpr uint32_t SR_E1 = 0x1000;
// Mock E1 behaviour (FAKE): counters at CHC+4*S_*; FWD role forwards instantly on OUT-slot header write; other results are canned.
constexpr uint32_t CHC = 0x8100, RING_OUT = 0x10000, RING_IN = 0x20000, CAR = 0x1040;
enum { S_FWD = 4, S_INSEEN = 5, S_REFL = 6, S_TXFAIL = 7, S_VCHK = 8, S_VBAD = 9, S_VWORD = 10, S_VGOT = 11, S_VEXP = 12, S_OUTIDX = 13, S_ININDEX = 14, S_BYTES = 15 };
enum { M_L0_NOFENCE = 2, M_L0_FENCE = 3, M_L0_CSR = 4, M_FWD = 5, M_REFLECT = 6 };
enum { F_FENCE = 1, F_CSR = 2, F_REGEN = 4 };
static uint32_t pkt_bytes(uint32_t tcms) { return tcms == 0u ? 16u : (tcms == 1u ? 32u : (tcms - 1u) * 64u); }
static uint32_t mk_hdr(uint32_t nonce, uint32_t tcms, uint32_t state) { return 0x2cu | ((nonce & 0xffu) << 8) | (tcms << 16) | (state << 24); }
static uint32_t payload_word(uint32_t n, uint32_t w, uint32_t salt) { return (n * 0x01010101u) ^ (w * 0x9E3779B1u) ^ (salt * 0x00010001u); }
struct MockPort : Port {
  Fabric& f; int dev; MockPort(Fabric& fb, int d) : f(fb), dev(d) {}
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override {
    if (a == CHC + 4 * S_REFL && f.get(dev, x, CTL + 0x10) == M_REFLECT && f.get(dev, x, CTL) == 1) f.put(dev, x, a, f.get(dev, x, a) + 1000);   // FAKE progress
    v = f.get(dev, x, a); return true;
  }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override {
    f.put(dev, x, a, v);
    if (a == TXQ2 + 4 && v == 2) f.cmd(dev, x);
    if (a == 0xFFB121B0 && !(v & 0x1000)) f.put(dev, x, 0x8008, 0xE1E1E1E1u);   // fake E1 comes up
    uint32_t mode = f.get(dev, x, CTL + 0x10);
    if (a == CTL + 4 && v == 1) {                                                 // fake E1 results on stop
      if (mode == M_L0_NOFENCE) { f.put(dev, x, CTL + 0x34, 3000000); f.put(dev, x, CTL + 0x38, 100000); f.put(dev, x, CTL + 0x3c, 500000); }
      if (mode == M_L0_FENCE) { f.put(dev, x, CTL + 0x34, 1500000); f.put(dev, x, CTL + 0x38, 580000); f.put(dev, x, CTL + 0x3c, 0); }
      if (mode == M_L0_CSR) { f.put(dev, x, CTL + 0x28, 0xC5C5C5C5u); f.put(dev, x, CTL + 0x2c, 0x80000008u); f.put(dev, x, CTL + 0x34, 3000000); f.put(dev, x, CTL + 0x38, 580000); f.put(dev, x, CTL + 0x3c, 0); }
      f.put(dev, x, CTL + 0xC, 1);
    }
    if (mode == M_FWD && f.get(dev, x, CTL) == 1 && a >= RING_OUT && a < RING_OUT + 4 * CAR && (a - RING_OUT) % CAR == 0) {   // FAKE instant forward
      uint32_t tcms = (v >> 16) & 0xff, bytes = pkt_bytes(tcms), slot = (a - RING_OUT) / CAR, p = dev ^ 1;
      if (f.get(dev, x, a + bytes - 4) == v) {
        for (uint32_t i = 0; i < bytes; i += 4) f.put(p, x, RING_IN + slot * CAR + i, f.get(dev, x, RING_OUT + slot * CAR + i));
        f.put(dev, x, CHC + 4 * S_FWD, f.get(dev, x, CHC + 4 * S_FWD) + 1);
        f.put(p, x, CHC + 4 * S_INSEEN, f.get(p, x, CHC + 4 * S_INSEEN) + 1);
      }
    }
    return true;
  }
};

// ---- the ONLY writes this tool may ever issue ----
std::vector<uint32_t> g_sr_origs, g_pc_origs, g_end_origs;
uint32_t g_e1_bytes = 0;
bool in_vec(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }
bool write_ok(uint32_t a, uint32_t v) {
  if (a == SOFT_RESET) { for (uint32_t o : g_sr_origs) if (v == o || v == (o & ~SR_E1)) return true; return false; }
  if (a == E1_PC) return v == E1_CODE || in_vec(g_pc_origs, v);
  if (a == E1_END) return v == E1_CODE + g_e1_bytes || in_vec(g_end_origs, v);
  if (a >= E1_CODE && a < E1_CODE + 0x1000 && !(a & 3)) return true;
  if (a >= CTL && a < CTL + 0x400 && !(a & 3)) return true;
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

// generated by e1order/build.sh -- 2660 bytes, link address 0x4000
static const uint32_t E1_IMAGE[] = {
  0xffb02137, 0x00000097, 0x00c080e7, 0x0000006f, 0xec010113, 0x12112e23,
  0x12812c23, 0x12912a23, 0x13212823, 0x13312623, 0x13412423, 0x13512223,
  0x13612023, 0x11712e23, 0x11812c23, 0x11912a23, 0x11a12823, 0x11b12623,
  0xffb92537, 0x01450593, 0x06b12023, 0x9e3785b7, 0x9b158893, 0x010105b7,
  0x10158593, 0x00b12e23, 0x000083b7, 0x10438d93, 0x000015b7, 0x04058093,
  0xe1e1e5b7, 0x1e158593, 0x00b3a423, 0xffe00913, 0x00200c93, 0xc5c5c5b7,
  0x5c558593, 0x00b12423, 0x06c10493, 0x02c00a93, 0x070005b7, 0x02c58593,
  0x02b12023, 0x000625b7, 0xa8158593, 0x04b12c23, 0x06112223, 0x03b12223,
  0x0003a583, 0xfe058ee3, 0x0003a623, 0x0103a583, 0x0183a783, 0x01c3a683,
  0x0203a603, 0x00c12c23, 0x0243a703, 0x00400613, 0x06d12423, 0x00d12a23,
  0x00c6e663, 0x00400613, 0x06c12423, 0xffb70613, 0xffc00693, 0x00d67463,
  0x00400713, 0xff958613, 0x03266a63, 0x0027f613, 0x02060663, 0x00812603,
  0x02c3a423, 0x00800613, 0x7c062073, 0x7c002673, 0x800006b7, 0x00d66633,
  0x02c3a623, 0xffe7f793, 0x0a80006f, 0xffe58613, 0x0acce063, 0x0143a603,
  0x00062283, 0x00400693, 0x02d59463, 0x00812683, 0x02d3a423, 0x00800693,
  0x7c06a073, 0x7c0026f3, 0x80000737, 0x00e6e6b3, 0x02d3a623, 0x00062283,
  0x00000713, 0x00000793, 0x00000813, 0x00300693, 0x04d59063, 0x0380006f,
  0x00062683, 0x00170713, 0x0056c333, 0x00603333, 0xfff2c293, 0x005682b3,
  0x006787b3, 0x40600333, 0x005372b3, 0x01028833, 0x00068293, 0x00300693,
  0x00d59463, 0x0ff0000f, 0x0043a683, 0xfc0682e3, 0x02e3aa23, 0x02f3ac23,
  0x0303ae23, 0x06d0006f, 0x00600613, 0x04e12623, 0x3ac58e63, 0x00500613,
  0x04c59ce3, 0x00000613, 0x0c012423, 0x0c012223, 0x0c012023, 0x0a012e23,
  0x0a012c23, 0x0a012a23, 0x0a012823, 0x0a012623, 0x0c012623, 0x06012623,
  0x0c012823, 0x06012823, 0x0c012a23, 0x06012a23, 0x0c012c23, 0x06012c23,
  0x0c012e23, 0x06012e23, 0x0e012023, 0x08012023, 0x0e012223, 0x08012223,
  0x0e012423, 0x08012423, 0x0e012623, 0x08012623, 0x0e012823, 0x08012823,
  0x0e012a23, 0x08012a23, 0x0e012c23, 0x08012c23, 0x0e012e23, 0x08012e23,
  0x10012023, 0x0a012023, 0x10012223, 0x0a012223, 0x10012423, 0x0a012423,
  0x0017f793, 0x04f12a23, 0x0140006f, 0x05c12603, 0x00160613, 0x000083b7,
  0x02c3a823, 0x05412583, 0x00059863, 0x0043a583, 0x00058c63, 0x7840006f,
  0x0ff0000f, 0x0043a583, 0x76059c63, 0x0ff0000f, 0x04c12e23, 0x01412583,
  0xfc0582e3, 0x00000593, 0x0200006f, 0xffe00913, 0x00200c93, 0x06c10493,
  0x00158593, 0x00efa023, 0x06812603, 0xfac580e3, 0x00200493, 0xffe00a13,
  0x00659e13, 0x01be0e33, 0xffce2903, 0x000e2803, 0x004e2983, 0x00259f93,
  0x0bc10e93, 0x01fe8eb3, 0x000ea403, 0x02140333, 0x01230cb3, 0x000cad03,
  0x0ffd7613, 0x00459293, 0x17561663, 0x018e0a93, 0x00ce0b13, 0x038e0b93,
  0x030e0c13, 0x018d5613, 0xffb60613, 0x15466863, 0x008d1693, 0x0186d693,
  0x04200613, 0x14d66063, 0x00669613, 0xfc060713, 0x00070613, 0x00100793,
  0x00f69463, 0x02000613, 0x00069463, 0x01000613, 0x019607b3, 0xffc7a383,
  0x11a39a63, 0x0096ea63, 0xfec7a683, 0x10070263, 0x01a68663, 0x0fc0006f,
  0x0e060c63, 0x0cc10093, 0x005080b3, 0x00241693, 0x00d080b3, 0x0000a683,
  0x0edd0063, 0x00000393, 0x00000793, 0x01330333, 0x40f60733, 0x5b000693,
  0x00d76463, 0x5b000713, 0x000626b7, 0xa8168693, 0x00852f03, 0x00ff1f13,
  0x000f5863, 0xfff68693, 0xfe0698e3, 0x0980006f, 0x0013f693, 0x00068663,
  0x00000693, 0x0080006f, 0x0006f6b3, 0x019783b3, 0x06012f03, 0x01e686b3,
  0x0076a023, 0x00f306b3, 0x00d52e23, 0x00e52c23, 0x00952223, 0x00f707b3,
  0x00100393, 0xf8c7eae3, 0x01a0a023, 0x000b2683, 0x00140413, 0x00168693,
  0x00db2023, 0x000ba683, 0x04c12703, 0x00e43733, 0x40e00733, 0x00877433,
  0x00c68633, 0x00cba023, 0x008c2023, 0x06412083, 0x02140333, 0x01230cb3,
  0x000cad03, 0x0ffd7613, 0x02c00693, 0xecd600e3, 0x0140006f, 0x000aa603,
  0x00160613, 0x00caa023, 0x06412083, 0x0ac10613, 0x01f60fb3, 0x000fa703,
  0x008ea023, 0x021707b3, 0x010787b3, 0x0007ae83, 0x0ffef613, 0x02c00a93,
  0xe1561ae3, 0x010e0313, 0x034e0e13, 0xffe00913, 0x00200c93, 0x06c10493,
  0x04c12403, 0x018ed613, 0xffb60613, 0xdf266ee3, 0x008e9613, 0x01865613,
  0x04200693, 0xdec6e6e3, 0x00661693, 0xfc068693, 0x00068393, 0x00100f13,
  0x01e61463, 0x02000393, 0x00061463, 0x01000393, 0x00f387b3, 0xffc7af03,
  0xdddf10e3, 0x01966a63, 0xfec7a603, 0xda068ae3, 0x01d60663, 0xdadff06f,
  0xda0384e3, 0x00548633, 0x00271693, 0x00d60633, 0x00062683, 0xd8de8ae3,
  0x01d62023, 0x00170713, 0x00032603, 0x008736b3, 0x40d006b3, 0x00e6f733,
  0x00160613, 0x00c32023, 0x00ee2023, 0x021707b3, 0x010787b3, 0x0007ae83,
  0x0ffef613, 0xf5560ce3, 0xd59ff06f, 0x00000f13, 0x00000693, 0x0047f593,
  0x02b12623, 0x0025d593, 0x01812603, 0x00c03633, 0x00b675b3, 0x02b12423,
  0x06012c23, 0x06012a23, 0x06012823, 0x06012623, 0x0c012623, 0x0c012823,
  0x0c012a23, 0x0c012c23, 0x0c012e23, 0x0e012023, 0x0e012223, 0x0e012423,
  0x0e012623, 0x0e012823, 0x0e012a23, 0x0e012c23, 0x0e012e23, 0x10012023,
  0x10012223, 0x10012423, 0x0017f793, 0x00f12623, 0x0140006f, 0x01012683,
  0x00168693, 0x000083b7, 0x02d3a823, 0x00c12583, 0x00059863, 0x0043a583,
  0x00058c63, 0x4040006f, 0x0ff0000f, 0x0043a583, 0x3e059c63, 0x0ff0000f,
  0x00d12823, 0x01412583, 0xfc0582e3, 0x00000613, 0x0140006f, 0x00f9a023,
  0x00160613, 0x06812583, 0xfab606e3, 0x00661813, 0x01b80833, 0x00082a03,
  0x00482b03, 0x00261c13, 0x018489b3, 0x0009a783, 0x02178e33, 0x014e05b3,
  0x0005ae83, 0x0ffef693, 0xfd5692e3, 0x01080393, 0x01c80693, 0x04d12a23,
  0x02080693, 0x04d12823, 0x02480313, 0x02880f93, 0x02c80413, 0x01480713,
  0x03880293, 0x03480d13, 0x018ed693, 0xffb68693, 0xf926e6e3, 0x008e9693,
  0x0186db93, 0x04200693, 0xf776eee3, 0x05a12023, 0x04512223, 0x04e12423,
  0x006b9693, 0xfc068693, 0x00068d13, 0x00100713, 0x00eb9463, 0x02000d13,
  0x000b9463, 0x01000d13, 0x00bd0733, 0xffc72283, 0xf5d292e3, 0x019bea63,
  0xfec72703, 0xf2068ce3, 0x01d70663, 0xf31ff06f, 0xf20d06e3, 0x02812e23,
  0x00461693, 0x0cc10713, 0x00d706b3, 0x00279413, 0x00868433, 0x00042683,
  0xf0de86e3, 0x03f12a23, 0x02612c23, 0x05612e23, 0x002d5713, 0xffb70d93,
  0xfff70093, 0x000d8313, 0x00100693, 0x0176e463, 0x00008313, 0x0003a683,
  0x008edb13, 0x0187efb3, 0x00168693, 0x02712823, 0x00d3a023, 0x02812683,
  0x0e068063, 0x000f0693, 0x001f0693, 0x00068f13, 0x01812283, 0x0c56e663,
  0x00200493, 0x00070a93, 0x00e4e463, 0x00200a93, 0x0ffb7693, 0x01c12283,
  0x025686b3, 0x010f9293, 0x01f282b3, 0x00d2cf33, 0x00458293, 0x40e484b3,
  0x00100393, 0x9e3786b7, 0x9b168693, 0x0140006f, 0x00138393, 0x00428293,
  0x011686b3, 0x047a8e63, 0x00748933, 0x00100c93, 0xff9904e3, 0xfe7302e3,
  0x0002a903, 0x00df4cb3, 0xfd990ce3, 0x05412283, 0x0002a683, 0x00168693,
  0x00d2a023, 0x05012683, 0x0006a283, 0x02029463, 0x03812683, 0x0076a023,
  0x03412683, 0x0126a023, 0x03c12683, 0x0196a023, 0x05012683, 0x0080006f,
  0x05412683, 0x0006a283, 0x00000f13, 0x00128293, 0x0056a023, 0xffe00913,
  0x00200c93, 0x06c10493, 0x001b0b13, 0x0ffb7b13, 0x02c12683, 0x06068663,
  0x00200293, 0x00070693, 0x00e2e463, 0x00200693, 0x01c12283, 0x025b02b3,
  0x010f9393, 0x01f383b3, 0x0053c3b3, 0x00458f93, 0xfff30313, 0xffe70a93,
  0xfff6c693, 0x00e68733, 0x00088693, 0x0180006f, 0x004f8f93, 0x011686b3,
  0xfffa8a93, 0xfff30313, 0x00ea8c63, 0xfe0a86e3, 0xfe0304e3, 0x00d3c2b3,
  0x005fa023, 0xfddff06f, 0xff0006b7, 0x00def6b3, 0x008b1b13, 0x010b9713,
  0x40d70733, 0x02012303, 0x006b0333, 0x00670333, 0x019be863, 0x002d9d93,
  0x00bd8db3, 0x006da023, 0x00209093, 0x00b080b3, 0x0060a023, 0x0065a023,
  0x0005a703, 0x00000393, 0x00000e93, 0x05c12b03, 0x016e0e33, 0x02412d83,
  0x06412083, 0x02c00a93, 0x41dd0fb3, 0x5b000693, 0x00dfe463, 0x5b000f93,
  0x05812683, 0x00852283, 0x00f29293, 0x0002d863, 0xfff68693, 0xfe0698e3,
  0x0ac0006f, 0x0013f693, 0x00068663, 0x00000693, 0x0080006f, 0x000776b3,
  0x00be82b3, 0x06012383, 0x007686b3, 0x0056a023, 0x01de06b3, 0x00d52e23,
  0x01f52c23, 0x01952223, 0x01df8eb3, 0x00100393, 0xf9aeece3, 0x00642023,
  0x04812703, 0x00072583, 0x00178793, 0x00158593, 0x00b72023, 0x04412283,
  0x0002a583, 0x04c12683, 0x00d7b6b3, 0x40d006b3, 0x00f6f7b3, 0x01a585b3,
  0x00b2a023, 0x04012d03, 0x00fd2023, 0x02178e33, 0x014e05b3, 0x0005ae83,
  0x0ffef693, 0x03012383, 0x03812303, 0x03412f83, 0x03c12403, 0xcb5686e3,
  0xc3dff06f, 0x00f9a023, 0x01882583, 0x00158593, 0x00b82c23, 0xc2dff06f,
  0x00100593, 0x00b3a623, 0x0003a583, 0xfe059ee3, 0xe60ff06f,
};


// ---- E1 lifecycle (one E1State per card) ----
volatile sig_atomic_t g_abort = 0;
void on_sigint(int) { g_abort = 1; }

struct E1State { bool deployed = false; Side* s = nullptr; uint32_t sr = 0xFFFFFFFFu, pc = 0, end = 0; };

bool e1_deploy(Side& s, E1State& st, bool force) {
  st.sr = R(s, SOFT_RESET); st.pc = R(s, E1_PC); st.end = R(s, E1_END);
  g_e1_bytes = (uint32_t)sizeof E1_IMAGE;
  if (!in_vec(g_sr_origs, st.sr)) g_sr_origs.push_back(st.sr);
  if (!in_vec(g_pc_origs, st.pc)) g_pc_origs.push_back(st.pc);
  if (!in_vec(g_end_origs, st.end)) g_end_origs.push_back(st.end);
  printf("  %s: soft-reset=0x%08x (E1 %s)  E1 reset PC=0x%x end PC=0x%x\n", s.name.c_str(), st.sr, (st.sr & SR_E1) ? "in reset" : "RUNNING", st.pc, st.end);
  if (!(st.sr & SR_E1) && !force) { printf("  refusing: E1 is not in reset (someone else is using it); --force to override\n"); return false; }
  printf("  deploying %u-byte E1 image at L1 0x%x on %s\n", g_e1_bytes, E1_CODE, s.name.c_str());
  if (!g_go) {
    printf("  (dry-run) would: write image, zero control block 0x%x..0x%x, set E1 PC=0x%x END=0x%x, arm NOC_CLEANUP(0x%x<=0x%x), clear soft-reset bit 12\n",
           CTL, CTL + 0x3ff, E1_CODE, E1_CODE + g_e1_bytes, SOFT_RESET, st.sr);
    return true;
  }
  for (uint32_t a = CTL; a < CTL + 0x400; a += 4) Wq(s, a, 0);
  for (size_t i = 0; i < sizeof E1_IMAGE / 4; ++i) Wq(s, E1_CODE + 4 * (uint32_t)i, E1_IMAGE[i]);
  unsigned bad = 0;
  for (size_t i = 0; i < sizeof E1_IMAGE / 4; ++i) bad += (R(s, E1_CODE + 4 * (uint32_t)i) != E1_IMAGE[i]);
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

// run-control helpers
bool e1_start(Side& s, uint32_t mode, uint32_t flags, uint32_t nr, uint32_t ve) {
  for (uint32_t a = CTL + 0x28; a < CTL + 0x400; a += 4) Wq(s, a, 0);        // stats, results; NOT +0x08 (ready)
  Wq(s, CTL + 0x04, 0); Wq(s, CTL + 0x0c, 0);
  Wq(s, CTL + 0x10, mode); Wq(s, CTL + 0x18, flags); Wq(s, CTL + 0x1c, 1); Wq(s, CTL + 0x20, ve); Wq(s, CTL + 0x24, nr);
  Wq(s, CTL + 0x14, DST_L1);
  Wq(s, CHC, RING_OUT); Wq(s, CHC + 4, RING_IN); Wq(s, CHC + 8, RING_IN);      // out, in, peer's in (same address on both cards)
  for (uint32_t i = 0; i < 4; ++i) { Wq(s, RING_OUT + i * CAR, 0); Wq(s, RING_IN + i * CAR, 0); }
  return true;
}
inline void e1_go(Side& s) { (void)R(s, CTL); Wq(s, CTL + 0x00, 1); }
bool e1_stop(Side& s) {
  Wq(s, CTL + 0x04, 1);
  bool done = false;
  for (int i = 0; i < 4000 && !done; ++i) { done = R(s, CTL + 0x0c) == 1; if (!done) std::this_thread::sleep_for(std::chrono::microseconds(50)); }
  Wq(s, CTL + 0x00, 0);
  return done;
}
uint32_t stat(Side& s, uint32_t k) { return R(s, CHC + 4 * k); }

// ---- STAGE 0 ----
struct L0Res { bool ok = false; uint32_t writes = 0, polls = 0, changes = 0, missed = 0, csr_mark = 0, csr_rb = 0; double secs = 0, wr_per_s = 0; };
L0Res l0_run(Side& rx, uint32_t mode, unsigned ms) {
  L0Res r;
  Wq(rx, DST_L1, 0);
  e1_start(rx, mode, 0, 4, 0);
  auto t0 = Clock::now();
  e1_go(rx);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  auto tw = Clock::now(); uint32_t cnt = 0;
  while (us_since(tw) < ms * 1000.0) { for (int i = 0; i < 64; ++i) Wq(rx, DST_L1, ++cnt); }
  double wsecs = us_since(tw) / 1e6;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  bool done = e1_stop(rx);
  r.secs = us_since(t0) / 1e6;
  r.csr_mark = R(rx, CTL + 0x28); r.csr_rb = R(rx, CTL + 0x2c);
  if (!done) return r;
  r.ok = true; r.writes = cnt; r.wr_per_s = cnt / wsecs;
  r.polls = R(rx, CTL + 0x34); r.changes = R(rx, CTL + 0x38); r.missed = R(rx, CTL + 0x3c);
  return r;
}
void report_l0(const char* what, const L0Res& r) {
  printf("  [l0 %s] host wrote %u values (%.2f M/s); E1 polled %u times (%.1f ns/poll), saw %u changes, MISSED %u values (%.1f%%)\n", what, r.writes, r.wr_per_s / 1e6, r.polls,
         r.polls ? r.secs / r.polls * 1e9 : 0.0, r.changes, r.missed, r.writes ? 100.0 * r.missed / r.writes : 0.0);
}
bool stage0(Side& B, E1State& e1B, bool force) {
  printf("\n--- STAGE 0: L0 data-cache polling test on E1 (card %d) ---\n", B.dev);
  L0Res a = l0_run(B, M_L0_NOFENCE, 300), b = l0_run(B, M_L0_FENCE, 300);
  if (a.ok) report_l0("no fence       ", a); else printf("  no-fence run: E1 did not stop\n");
  if (b.ok) report_l0("fence          ", b); else printf("  fence run: E1 did not stop\n");
  printf("  (running the CSR 0x7c0 DisLowCache experiment LAST; a missing/illegal CSR may hang E1 -- the tool will reset and redeploy it)\n");
  L0Res c = l0_run(B, M_L0_CSR, 300);
  if (c.ok) {
    report_l0("CSR l0off,nofence", c);
    printf("  CSR 0x7c0 readback after setting bit 3: 0x%08x (bit31 is our 'access completed' tag; bit 3 %s)\n", c.csr_rb, (c.csr_rb & 8u) ? "reads back SET" : "reads back CLEAR");
  } else {
    printf("  CSR run: E1 did NOT stop.  CSR marker word 0x%08x readback word 0x%08x => %s\n", c.csr_mark, c.csr_rb,
           c.csr_mark == 0xC5C5C5C5u && c.csr_rb == 0 ? "E1 died/hung AT the CSR access (CSR 0x7c0 write unsupported)" : "hang cause unknown");
  }
  // fresh E1 (also clears any L0 setting) for the next stages
  e1_reset(e1B);
  bool ok = e1_deploy(B, e1B, force);
  double pn = a.ok && a.writes ? 100.0 * a.missed / a.writes : -1, pw = b.ok && b.writes ? 100.0 * b.missed / b.writes : -1, pc = c.ok && c.writes ? 100.0 * c.missed / c.writes : -1;
  printf("  SUMMARY stage 0: missed%% no-fence %.1f | fence %.1f | CSR-l0off-no-fence %.1f\n", pn, pw, pc);
  if (c.ok && pc >= 0 && pn > 5.0 && pc < 1.0) printf("  => CSR 0x7c0 bit 3 DOES disable the stale-L0 effect on E1: polling without fence is viable (ns/poll above)\n");
  else if (c.ok && pn > 5.0) printf("  => CSR executed but stale misses persist (%.1f%%): keep using fences on E1\n", pc);
  else if (!c.ok) printf("  => do not use the CSR on E1; keep using fences\n");
  return ok && a.ok && b.ok;
}

// ---- STAGE 1 helpers ----
uint32_t pw1(int dev, uint32_t i, uint32_t n, uint32_t w) { return payload_word(n, w, ((uint32_t)dev << 8) | i); }
uint32_t tcms_for(uint32_t bytes) { return bytes == 16 ? 0 : bytes == 32 ? 1 : bytes / 64 + 1; }
bool size_ok(uint32_t bytes) { return bytes == 16 || bytes == 32 || (bytes >= 64 && bytes % 64 == 0 && bytes <= 4160); }

// write a complete car at 'slot' on s, header written last (posted writes through one mapping stay ordered)
void put_car(Side& s, uint32_t slot, uint32_t nonce, uint32_t tcms, uint32_t state, int dev, uint32_t i, bool useSalt1b) {
  uint32_t bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
  uint32_t h = mk_hdr(nonce, tcms, state);
  for (uint32_t w = 1; w < words; ++w) {
    if (w == fi || w == ai) continue;
    Wq(s, slot + 4 * w, useSalt1b ? payload_word(nonce, w, i) : pw1(dev, i, nonce, w));
  }
  if (tcms >= 2) Wq(s, slot + 4 * ai, h);
  Wq(s, slot + 4 * fi, h);
  (void)R(s, slot + 4 * fi);                   // flush: payload+footer visible before the header
  Wq(s, slot, h);
}
// verify a car at slot against the stage 1a pattern; returns 0 ok else (word index + 1)
uint32_t verify_car(Side& s, uint32_t slot, uint32_t nonce, uint32_t tcms, uint32_t state, int dev, uint32_t i) {
  uint32_t bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi, h = mk_hdr(nonce, tcms, state);
  if (R(s, slot) != h) return 1;
  for (uint32_t w = 1; w < words; ++w) {
    uint32_t exp = (w == fi || w == ai) ? h : pw1(dev, i, nonce, w);
    if (R(s, slot + 4 * w) != exp) return w + 1;
  }
  return 0;
}

// ---- STAGE 1a ----
bool stage1a(Side& A, Side& B, const std::vector<uint32_t>& sizes, uint32_t rounds, uint32_t flags) {
  printf("\n--- STAGE 1a: host-as-hub car forwarding, both directions, 4-car bursts, %u rounds per size ---\n", rounds);
  bool all = true;
  for (uint32_t bytes : sizes) {
    if (g_abort) break;
    uint32_t tcms = tcms_for(bytes), chunks = (bytes + 1455) / 1456;
    e1_start(A, M_FWD, flags, 4, 0); e1_start(B, M_FWD, flags, 4, 0);
    e1_go(A); e1_go(B);
    uint32_t bad = 0, timeouts = 0, firstBadRound = 0, firstBadWord = 0; int firstBadDir = -1;
    auto t0 = Clock::now();
    for (uint32_t r = 0; r < rounds && !g_abort; ++r) {
      uint32_t nonce[4];
      for (uint32_t i = 0; i < 4; ++i) nonce[i] = ((r * 7 + i * 37) % 255) + 1;
      for (uint32_t i = 0; i < 4; ++i) { put_car(A, RING_OUT + i * CAR, nonce[i], tcms, 3, A.dev, i, false); put_car(B, RING_OUT + i * CAR, nonce[i], tcms, 3, B.dev, i, false); }
      for (uint32_t i = 0; i < 4; ++i) {
        for (int dir = 0; dir < 2; ++dir) {
          Side& dst = dir ? A : B; Side& src = dir ? B : A;
          uint32_t want = mk_hdr(nonce[i], tcms, 3);
          auto lim = Clock::now() + std::chrono::milliseconds(200);
          bool got = false;
          while (Clock::now() < lim) { if (R(dst, RING_IN + i * CAR) == want) { got = true; break; } }
          if (!got) { ++timeouts; if (!firstBadRound && firstBadDir < 0) { firstBadRound = r + 1; firstBadDir = dir; } continue; }
          uint32_t vb = verify_car(dst, RING_IN + i * CAR, nonce[i], tcms, 3, src.dev, i);
          if (vb) { ++bad; if (firstBadDir < 0) { firstBadRound = r + 1; firstBadDir = dir; firstBadWord = vb - 1; } }
        }
      }
      if (timeouts > 20) break;
    }
    double secs = us_since(t0) / 1e6;
    (void)e1_stop(A); (void)e1_stop(B);
    uint32_t n = rounds * 4;
    printf("  size %4u B (tcms %2u, %u chunk%s): host-verified %u cars/dir expected; bad %u, timeouts %u; %.3f s => %.1f us/round (host PCIe-dominated), %.2f MB/s total payload\n",
           bytes, tcms, chunks, chunks > 1 ? "s" : "", n, bad, timeouts, secs, secs * 1e6 / rounds, 2.0 * n * bytes / secs / 1e6);
    for (Side* s : {&A, &B})
      printf("    %s E1: fwd %u  in-seen %u  txfail %u  bytes %u  (expect fwd=in-seen=%u)\n", s->name.c_str(), stat(*s, S_FWD), stat(*s, S_INSEEN), stat(*s, S_TXFAIL), stat(*s, S_BYTES), n);
    if (firstBadDir >= 0) printf("    first problem: round %u dir %s word %u\n", firstBadRound, firstBadDir ? "B->A" : "A->B", firstBadWord);
    bool ok = !bad && !timeouts && stat(A, S_FWD) == n && stat(B, S_FWD) == n && stat(A, S_INSEEN) == n && stat(B, S_INSEEN) == n && !stat(A, S_TXFAIL) && !stat(B, S_TXFAIL);
    printf("    -> %s\n", ok ? "OK" : "FAIL");
    all = all && ok;
    if (!ok) break;
  }
  return all;
}

// ---- STAGE 1b ----
bool stage1b(Side& A, Side& B, const std::vector<uint32_t>& sizes, const std::vector<uint32_t>& ks, unsigned dur_ms, bool regen, uint32_t ve, uint32_t flags) {
  printf("\n--- STAGE 1b: autonomous REFLECT throughput (host only seeds cars and samples counters), %u ms per point%s ---\n", dur_ms, regen ? ", payload regen+verify ON" : "");
  bool all = true;
  uint32_t fl = flags | (regen ? F_REGEN : 0);
  for (uint32_t bytes : sizes) for (uint32_t K : ks) {
    if (g_abort) return false;
    uint32_t tcms = tcms_for(bytes);
    e1_start(A, M_REFLECT, fl, K, ve); e1_start(B, M_REFLECT, fl, K, ve);
    e1_go(A); e1_go(B);
    for (uint32_t i = 0; i < K; ++i) put_car(A, RING_IN + i * CAR, i + 1, tcms, 3, 0, i, true);   // seed on A's IN ring; salt = slot index
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    uint32_t a0 = stat(A, S_REFL), b0 = stat(B, S_REFL); auto t0 = Clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(dur_ms));
    uint32_t a1 = stat(A, S_REFL), b1 = stat(B, S_REFL); double secs = us_since(t0) / 1e6;
    bool d1 = e1_stop(A), d2 = e1_stop(B);
    double hops = (double)(a1 - a0) + (double)(b1 - b0);
    uint32_t tf = stat(A, S_TXFAIL) + stat(B, S_TXFAIL), vb = stat(A, S_VBAD) + stat(B, S_VBAD), vc = stat(A, S_VCHK) + stat(B, S_VCHK);
    printf("  size %4u B K=%u: %.3f M hops/s (A %u B %u in %.3f s) => %.0f ns/hop aggregate, %.1f MB/s; txfail %u%s%s", bytes, K, hops / secs / 1e6, a1 - a0, b1 - b0, secs,
           hops > 0 ? secs / hops * 1e9 : 0.0, hops * bytes / secs / 1e6, tf, d1 && d2 ? "" : "  (E1 stop not acked!)", hops > 0 ? "" : "  STALLED");
    if (regen) printf("  verify: checks %u BAD %u", vc, vb);
    printf("\n");
    if (regen && vb) printf("    first bad on A: word %u got %08x exp %08x; on B: word %u got %08x exp %08x\n", stat(A, S_VWORD), stat(A, S_VGOT), stat(A, S_VEXP), stat(B, S_VWORD), stat(B, S_VGOT), stat(B, S_VEXP));
    if (hops == 0 || tf || vb || !d1 || !d2) all = false;
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

void usage() {
  fprintf(stderr,
          "ethfwd1 -- eth-seam stages 0/1: E1 car forwarder over one link (cards A and B both run E1)\n"
          "  --link A:B:X          cards A and B, ETH tile NoC0 X (e.g. 0:1:6)\n"
          "  --go                  actually write (default: dry run, reads only)\n"
          "  --stage0 --stage1a --stage1b   select stages (default: all three)\n"
          "  --sizes a,b,c         car sizes in bytes: 16, 32 or a multiple of 64 up to 4160 (default 64,576,4160)\n"
          "  --rounds N            stage 1a rounds per size (default 2000; each round = 4 cars per direction)\n"
          "  --ks 1,2,4            stage 1b cars in flight (default 1,2,4)\n"
          "  --dur-ms N            stage 1b measurement time per point (default 1000)\n"
          "  --regen               stage 1b: E1 regenerates and periodically verifies payload each hop\n"
          "  --verify-every N      with --regen: verify every Nth car (default 16)\n"
          "  --csr                 stages 1: E1 sets DisLowCache (CSR 0x7c0 bit3) and polls without fence (only after stage 0 shows it works)\n"
          "  --keep                leave queue 2 / TX header 1 configured\n"
          "  --force               proceed even if queue 2 is not pristine or E1 is not in reset\n"
          "  --verbose             print every write\n"
          "  --mock                logic test against a FAKE model (all E1 results are FAKE)\n");
}

}  // namespace

int main(int argc, char** argv) {
  bool keep = false, force = false, mock = false, haveLink = false, regen = false, csr = false, s0 = false, s1a = false, s1b = false;
  LinkSpec ls{0, 0, 0};
  uint32_t rounds = 2000, ve = 16; unsigned durMs = 1000;
  std::vector<uint32_t> sizes = {64, 576, 4160}, ks = {1, 2, 4};
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i]; unsigned v = 0, q = 0, r = 0;
    if (a == "--link" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u:%u", &v, &q, &r) == 3) { ls = {(int)v, (int)q, r}; haveLink = true; ++i; }
    else if (a == "--sizes" && i + 1 < argc) sizes = parse_list(argv[++i]);
    else if (a == "--ks" && i + 1 < argc) ks = parse_list(argv[++i]);
    else if (a == "--rounds" && i + 1 < argc && parse_u(argv[i + 1], v)) { rounds = v; ++i; }
    else if (a == "--dur-ms" && i + 1 < argc && parse_u(argv[i + 1], v)) { durMs = v; ++i; }
    else if (a == "--verify-every" && i + 1 < argc && parse_u(argv[i + 1], v)) { ve = v; ++i; }
    else if (a == "--go") g_go = true;
    else if (a == "--stage0") s0 = true;
    else if (a == "--stage1a") s1a = true;
    else if (a == "--stage1b") s1b = true;
    else if (a == "--regen") regen = true;
    else if (a == "--csr") csr = true;
    else if (a == "--keep") keep = true;
    else if (a == "--force") force = true;
    else if (a == "--verbose") g_verbose = true;
    else if (a == "--mock") mock = true;
    else { usage(); return 2; }
  }
  if (!s0 && !s1a && !s1b) s0 = s1a = s1b = true;
  if (!haveLink || ls.a == ls.b) { usage(); return 2; }
  if (rounds == 0 || ve == 0 || durMs == 0) { fprintf(stderr, "bad --rounds/--verify-every/--dur-ms\n"); return 2; }
  for (uint32_t s : sizes) if (!size_ok(s)) { fprintf(stderr, "bad size %u (16, 32, or multiple of 64 up to 4160)\n", s); return 2; }
  for (uint32_t k : ks) if (k < 1 || k > 4) { fprintf(stderr, "bad K %u (1..4)\n", k); return 2; }
  g_verbose = g_verbose || !g_go;

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
  // header entry 1 must be selected before ANY send
  for (Side* s : {&A, &B}) W(*s, TXQ2 + 0x80, 1u << 8);

  bool pass = true; E1State eA, eB;
  if (g_go) {
    printf("\n--- smoke (64 B tagged, both directions) ---\n");
    uint32_t g1 = tagged_window(A, B, 64, 1, 1, nullptr), g2 = tagged_window(B, A, 64, 1, 2, nullptr);
    printf("  A->B %s, B->A %s\n", g1 == 1 ? "OK" : "FAIL", g2 == 1 ? "OK" : "FAIL"); pass = g1 == 1 && g2 == 1;
  }
  printf("\n--- E1 deploy ---\n");
  bool up = pass && e1_deploy(B, eB, force);
  if (up && (s1a || s1b)) up = e1_deploy(A, eA, force);
  if (!g_go) {
    printf("\nDry run: would run stages%s%s%s.  Re-run with --go.\n", s0 ? " 0" : "", s1a ? " 1a" : "", s1b ? " 1b" : "");
    return 0;
  }
  uint32_t fl = csr ? F_CSR : F_FENCE;
  if (up) {
    if (s0 && pass) { if (!stage0(B, eB, force)) pass = false; }
    if (s1a && pass) { if (!stage1a(A, B, sizes, rounds, fl)) pass = false; }
    if (s1b && pass) { if (!stage1b(A, B, sizes, ks, durMs, regen, ve, fl)) pass = false; }
  } else pass = false;

  e1_reset(eA); e1_reset(eB);
  printf("\n--- final queue-2 state ---\n");
  for (Side* s : {&A, &B}) printf("  %s TXQ2 ctrl=%x xfer=%u | RXQ2 ctrl=%x seq=%u rseq=%u drops=%u\n", s->name.c_str(), R(*s, TXQ2), R(*s, TXQ2 + 0x30), R(*s, RXQ2), R(*s, RXQ2 + 0x40), R(*s, RXQ2 + 0x44), R(*s, RXQ2 + 0x4c));
  if (!keep) { printf("\n--- restore ---\n"); restore(A); restore(B); }
  printf("\nRESULT: %s (%d write/readback problems)\n", (pass && !g_bad) ? "PASS" : "FAIL/INCONCLUSIVE", g_bad);
  return (pass && !g_bad) ? 0 : 1;
}
