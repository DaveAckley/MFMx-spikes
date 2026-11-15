// ethlinktest11.cpp -- WRITES (small, allowlisted) to Blackhole ETH tiles on several cards, to measure a
// custom TT-link path (TXQ2 -> peer RXQ2, unicast DA) driven from the host over PCIe.
// Successor to ethlinktest (which passed a 64-byte smoke test each way on card0<->card1 E10).
//
// Phases per run: (1) smoke on every link, (2) one-way latency as seen by the host, (3) burst throughput
// per packet size, round-robin across all given links, both directions, with content + counter checks,
// (4) a freshly-tagged window re-verify after each burst.
//
// IMPORTANT LIMITS: every send is issued by the host over PCIe (src/dst writes, a status read, a CMD write),
// so throughput numbers are LOWER BOUNDS dominated by host MMIO cost, and "latency" includes a PCIe read
// poll (the tool calibrates one PCIe read and prints it so you can subtract).  Receive-side packet counting
// uses RXQ2 +0x40, which may be 8 bits wide (compared mod 256) -- treat counts as consistency checks, not proof.
//
// SAFETY: default is a DRY RUN (reads only).  --go enables writes.  Every write is checked against an
// allowlist (write_ok): queue-2 regs, TXHDR[1], and L1 scratch 0x10000..0x6FFFF.  Nothing touches queue 0,
// E0/E1 reset, the classifier, or MAC/PCS.  Every link must be Up and be each other's FW-reported peers.
// Queue state is zeroed again afterwards unless --keep.
//
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror ethlinktest11.cpp -o ethlinktest11
// Run:   ./ethlinktest11 --link 0:1:6                       # dry run, one link
//        ./ethlinktest11 --edge x01 --go                    # 4 links card0<->card1 (E10,E12,E13,E11)
//        ./ethlinktest11 --edge y02 --edge y13 --go         # y-axis edges
// Edges: x01,x23 use tiles X=6,7,10,11 (E10,E12,E13,E11); y02,y13 use X=3,4,12,13 (E4,E6,E9,E7).
//
// ioctl plumbing is copied from ethprobe12.cpp.
#include <algorithm>
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
struct MockPort : Port {
  Fabric& f; int dev; MockPort(Fabric& fb, int d) : f(fb), dev(d) {}
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override { v = f.get(dev, x, a); return true; }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override {
    f.put(dev, x, a, v);
    if (a == TXQ2 + 4 && v == 2) f.cmd(dev, x);
    return true;
  }
};

// ---- the ONLY writes this tool may ever issue ----
bool write_ok(uint32_t a, uint32_t v) {
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

struct Dir { Side* tx; Side* rx; };

// Burst: N packets of S bytes per direction entry, issued round-robin across entries.  Window of Wn slots reused.
bool burst(std::vector<Dir>& dirs, uint32_t S, uint32_t N, uint32_t Wn, uint32_t tag) {
  size_t nd = dirs.size();
  std::vector<uint32_t> rb(nd), tx0(nd), drop0(nd);
  for (size_t i = 0; i < nd; ++i) { fill_src(*dirs[i].tx, S, Wn, tag); fill_dst(*dirs[i].rx, S, Wn); }
  for (size_t i = 0; i < nd; ++i) {
    (void)R(*dirs[i].tx, SRC_L1); (void)R(*dirs[i].rx, DST_L1);
    rb[i] = R(*dirs[i].rx, RXQ2 + 0x40); tx0[i] = R(*dirs[i].tx, TXQ2 + 0x30); drop0[i] = R(*dirs[i].rx, RXQ2 + 0x4c);
    W(*dirs[i].tx, TXQ2 + 0x18, S, false); W(*dirs[i].tx, TXQ2 + 0x80, 1u << 8, false);
    if (Wn == 1) { W(*dirs[i].tx, TXQ2 + 0x14, SRC_L1, false); W(*dirs[i].tx, TXQ2 + 0x1c, DST_L1, false); }
  }
  // Calibrate packets-per-transfer empirically: hardware max packet size counts the 32 B of Ethernet+TT-link
  // headers, so a 1488 B payload already splits in two (found on hardware; do not trust pkts_for()).
  std::vector<uint32_t> ppp(nd);
  for (size_t d = 0; d < nd; ++d) {
    issue(*dirs[d].tx, SRC_L1, DST_L1, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint32_t sq = R(*dirs[d].rx, RXQ2 + 0x40);
    ppp[d] = (sq - rb[d]) & 0xff;
    rb[d] = sq; tx0[d] = R(*dirs[d].tx, TXQ2 + 0x30);
    if (ppp[d] == 0 || ppp[d] > 8) { printf("  calibration odd on %s: rx seq advanced %u for one xfer\n", dirs[d].tx->name.c_str(), ppp[d]); ppp[d] = ppp[d] ? ppp[d] : 1; }
  }
  auto t0 = Clock::now();
  for (uint32_t i = 0; i < N; ++i) {
    uint32_t j = i % Wn;
    for (size_t d = 0; d < nd; ++d) issue(*dirs[d].tx, SRC_L1 + j * S, DST_L1 + j * S, Wn > 1);
  }
  double tIssue = us_since(t0);
  bool allok = true;
  printf("  S=%-5u N=%-6u W=%-3u pkts/xfer=%u(cal)  issue %.0f us (%.2f us/xfer/dir)\n", S, N, Wn, ppp[0], tIssue, tIssue / N / (double)nd);
  for (size_t d = 0; d < nd; ++d) {
    double t = wait_rx(*dirs[d].rx, rb[d], N * ppp[d], 3000, t0);
    uint32_t txd = R(*dirs[d].tx, TXQ2 + 0x30) - tx0[d], drops = R(*dirs[d].rx, RXQ2 + 0x4c) - drop0[d];
    uint32_t good = check_dst(*dirs[d].tx, *dirs[d].rx, S, Wn, tag);
    bool ok = t >= 0 && txd == N && drops == 0 && good == Wn;
    allok = allok && ok;
    printf("    %s->%s: %s  xfer_cnt+%u (want %u)  drops+%u  slots %u/%u  ", dirs[d].tx->name.c_str(), dirs[d].rx->name.c_str(), ok ? "OK  " : "FAIL", txd, N, drops, good, Wn);
    if (t >= 0) printf("done %.0f us -> %.1f MB/s (%.0f kpkt/s)\n", t, (double)N * S / t, N / t * 1000.0);
    else printf("RX seq never reached expected (rx seq %u, base %u)\n", R(*dirs[d].rx, RXQ2 + 0x40), rb[d]);
  }
  if (nd > 1) {
    // aggregate: total bytes over the slowest completion is approximate; report from issue time as a lower bound
    printf("    aggregate issue-limited rate: %.1f MB/s over %zu directions\n", (double)N * S * (double)nd / tIssue, nd);
  }
  return allok;
}

// One-way latency as seen by host: send 16 B with a unique tag, spin-read peer until the tag appears.
void latency(Side& tx, Side& rx, unsigned n) {
  std::vector<double> samples, reads;
  for (int i = 0; i < 2000; ++i) { auto t = Clock::now(); (void)R(rx, DST_L1); reads.push_back(us_since(t)); }
  std::sort(reads.begin(), reads.end());
  W(tx, TXQ2 + 0x14, SRC_L1, false); W(tx, TXQ2 + 0x18, 16, false); W(tx, TXQ2 + 0x1c, DST_L1, false); W(tx, TXQ2 + 0x80, 1u << 8, false);
  unsigned timeouts = 0;
  for (unsigned i = 0; i < n; ++i) {
    uint32_t tag = 0x1A700000u + i;
    for (unsigned w = 0; w < 4; ++w) Wq(tx, SRC_L1 + 4 * w, w == 0 ? tag : 0u);
    (void)R(tx, SRC_L1 + 12);
    if (!wait_idle(tx)) { ++g_bad; break; }
    auto t0 = Clock::now();
    Wq(tx, TXQ2 + 0x04, 2);
    double us = -1;
    while ((us = us_since(t0)) < 20000.0) { if (R(rx, DST_L1) == tag) break; }
    if (us >= 20000.0) { ++timeouts; continue; }
    samples.push_back(us);
  }
  std::sort(samples.begin(), samples.end());
  auto pc = [&](const std::vector<double>& v, double q) { return v.empty() ? -1.0 : v[std::min(v.size() - 1, (size_t)(q * (double)v.size()))]; };
  printf("  PCIe read calibration (one 32-bit read of peer L1): min %.2f  median %.2f  p99 %.2f us\n", pc(reads, 0), pc(reads, .5), pc(reads, .99));
  printf("  %s->%s 16 B: write-CMD-to-visible n=%zu (timeouts %u): min %.2f  median %.2f  p90 %.2f  p99 %.2f  max %.2f us\n", tx.name.c_str(),
         rx.name.c_str(), samples.size(), timeouts, pc(samples, 0), pc(samples, .5), pc(samples, .9), pc(samples, .99), pc(samples, 1.0));
  printf("    (host-observed; subtract ~half a PCIe read, and note the posted CMD write itself takes time to reach the tile)\n");
}

void restore(Side& s) {
  printf("  restoring %s (dev%d X=%u)\n", s.name.c_str(), s.dev, s.x);
  W(s, TXQ2 + 0x00, 0); W(s, RXQ2 + 0x00, 0);
  W(s, TXQ2 + 0x80, 0); W(s, TXQ2 + 0x84, 0);
  W(s, TXHDR1 + 0x10, 0); W(s, TXHDR1 + 0x14, 0); W(s, TXHDR1 + 0x18, 0); W(s, TXHDR1 + 0x1c, 0);
}

bool parse_u(const char* s, unsigned& o) { char* e = nullptr; unsigned long v = strtoul(s, &e, 0); if (e == s || *e) return false; o = (unsigned)v; return true; }
bool parse_mac(const char* s, uint32_t& major, uint32_t& minor) {
  unsigned o[6]; if (sscanf(s, "%x:%x:%x:%x:%x:%x", &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6) return false;
  major = (o[0] << 16) | (o[1] << 8) | o[2]; minor = (o[3] << 16) | (o[4] << 8) | o[5]; return true;
}
struct LinkSpec { int a, b; unsigned x; };

void usage() {
  fprintf(stderr,
          "ethlinktest11 -- WRITES to ETH tiles (link pairs) to measure a custom TT-link path from the host.\n"
          "  --link A:B:X          one link: cards A,B, ETH tile NoC0 X (same X both ends); repeatable\n"
          "  --edge NAME           x01 x23 (X=6,7,10,11) or y02 y13 (X=3,4,12,13): all four links of that edge; repeatable\n"
          "  --go                  actually write (default: dry run, reads only)\n"
          "  --sizes a,b,c         burst packet sizes, bytes, multiples of 16 (default 64,256,1024,1488)\n"
          "  --big                 also try 4096 (exercises hardware packet splitting; may misbehave)\n"
          "  --n N                 packets per direction per burst (default 20000)\n"
          "  --window W            slots reused during a burst (default 16; 1 = fixed addresses, fewest host writes)\n"
          "  --lat-n N             latency samples (default 2000; 0 = skip)\n"
          "  --keep                leave queue-2/TXHDR[1] configured afterwards (default: restore)\n"
          "  --force               proceed even if queue 2 / TXHDR[1] are not pristine\n"
          "  --da MAC              override destination MAC (unicast only; default = peer's FW-reported MAC)\n"
          "  --wait-ms N           settle time before content checks (default 50)\n"
          "  --verbose             print every write\n"
          "  --mock                exercise the logic against a FAKE model (x-edge pairs only)\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<LinkSpec> specs; bool keep = false, force = false, mock = false, haveDa = false, big = false;
  uint32_t dmaj = 0, dmin = 0, N = 20000, Wn = 16, latN = 2000;
  std::vector<uint32_t> sizes = {64, 256, 1024, 1488};
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i]; unsigned v = 0, q = 0, r = 0;
    if (a == "--link" && i + 1 < argc && sscanf(argv[i + 1], "%u:%u:%u", &v, &q, &r) == 3) { specs.push_back({(int)v, (int)q, r}); ++i; }
    else if (a == "--edge" && i + 1 < argc) {
      std::string e = argv[++i]; int p0 = -1, p1 = -1; std::vector<unsigned> xs;
      if (e == "x01") { p0 = 0; p1 = 1; xs = {6, 7, 10, 11}; } else if (e == "x23") { p0 = 2; p1 = 3; xs = {6, 7, 10, 11}; }
      else if (e == "y02") { p0 = 0; p1 = 2; xs = {3, 4, 12, 13}; } else if (e == "y13") { p0 = 1; p1 = 3; xs = {3, 4, 12, 13}; }
      else { usage(); return 2; }
      for (unsigned x : xs) specs.push_back({p0, p1, x});
    }
    else if (a == "--sizes" && i + 1 < argc) {
      sizes.clear(); char* s = argv[++i];
      for (char* t = strtok(s, ","); t; t = strtok(nullptr, ",")) sizes.push_back((uint32_t)strtoul(t, nullptr, 0));
    }
    else if (a == "--n" && i + 1 < argc && parse_u(argv[i + 1], v)) { N = v; ++i; }
    else if (a == "--window" && i + 1 < argc && parse_u(argv[i + 1], v)) { Wn = v; ++i; }
    else if (a == "--lat-n" && i + 1 < argc && parse_u(argv[i + 1], v)) { latN = v; ++i; }
    else if (a == "--wait-ms" && i + 1 < argc && parse_u(argv[i + 1], v)) { g_wait_ms = v; ++i; }
    else if (a == "--da" && i + 1 < argc && parse_mac(argv[i + 1], dmaj, dmin)) { haveDa = true; ++i; }
    else if (a == "--go") g_go = true;
    else if (a == "--keep") keep = true;
    else if (a == "--force") force = true;
    else if (a == "--big") big = true;
    else if (a == "--verbose") g_verbose = true;
    else if (a == "--mock") mock = true;
    else { usage(); return 2; }
  }
  if (specs.empty()) { usage(); return 2; }
  if (big) sizes.push_back(4096);
  if (haveDa && ((dmaj >> 16) & 1)) { fprintf(stderr, "--da must be unicast (LSB of first octet clear): multicast/broadcast would route to RXQ0 (base FW)\n"); return 2; }
  if (Wn == 0 || N == 0) { fprintf(stderr, "--n and --window must be >= 1\n"); return 2; }
  for (uint32_t s : sizes)
    if (s == 0 || s % 16 || s > 4096 || (uint64_t)s * Wn > DST_L1 - SRC_L1) { fprintf(stderr, "bad size %u (need multiple of 16, <=4096, size*window <= 0x%x)\n", s, DST_L1 - SRC_L1); return 2; }
  for (const LinkSpec& l : specs) if (l.a == l.b) { fprintf(stderr, "link endpoints must differ\n"); return 2; }
  if (!g_verbose && specs.size() == 1) g_verbose = true;

  Fabric fab; std::map<int, std::unique_ptr<Port>> ports;
  auto port_for = [&](int d) -> Port* {
    auto it = ports.find(d); if (it != ports.end()) return it->second.get();
    if (mock) { ports[d].reset(new MockPort(fab, d)); return ports[d].get(); }
    HwPort* hp = new HwPort(); std::string err; ports[d].reset(hp);
    if (!hp->open_dev("/dev/tenstorrent/" + std::to_string(d), err)) { fprintf(stderr, "dev%d: %s\n", d, err.c_str()); ports.erase(d); return nullptr; }
    return hp;
  };
  if (mock) { printf("*** MOCK MODE: FAKE model; nothing here is hardware ***\n"); for (const LinkSpec& l : specs) fab.init(l.x); }
  printf("%s\n", g_go ? "MODE: --go (WILL WRITE)" : "MODE: dry run (reads only; writes are printed, not issued)");

  std::vector<std::unique_ptr<Link>> links;
  for (const LinkSpec& l : specs) {
    Port* pa = port_for(l.a); Port* pb = port_for(l.b);
    if (!pa || !pb) return 1;
    char nm[48]; snprintf(nm, sizeof nm, "L%d.%d.%u", l.a, l.b, l.x);
    links.emplace_back(new Link{Side{pa, l.a, l.x, std::string(nm) + "a"}, Side{pb, l.b, l.x, std::string(nm) + "b"}, nm});
  }
  printf("--- validate ---\n");
  for (auto& L : links) {
    if (!validate(L->A, force) || !validate(L->B, force)) return 1;
    if (L->A.pmajor != L->B.major || L->A.pminor != L->B.minor || L->B.pmajor != L->A.major || L->B.pminor != L->A.minor) {
      printf("link check FAILED for %s: firmware peer records do not name each other\n", L->name.c_str()); return 1;
    }
  }
  printf("link check OK for %zu link(s)\n", links.size());
  {  // sanity: no tile used twice
    std::map<uint64_t, int> seen;
    for (auto& L : links) for (Side* s : {&L->A, &L->B}) if (seen[Fabric::key(s->dev, s->x, 0)]++) { printf("tile dev%d X=%u listed twice\n", s->dev, s->x); return 2; }
  }

  printf("\n--- configure (RX queues first, then headers, then TX queues) ---\n");
  for (auto& L : links) for (Side* s : {&L->A, &L->B}) { W(*s, RXQ2 + 0x0c, RXBUF_L1 >> 4); W(*s, RXQ2 + 0x10, 0x100); W(*s, RXQ2 + 0x18, 0xe); W(*s, RXQ2 + 0x00, 2); }
  for (auto& L : links) for (Side* s : {&L->A, &L->B}) {
    Side* o = (s == &L->A) ? &L->B : &L->A;
    uint32_t sa0, sa1, da0, da1;
    mac_words(s->major, s->minor, sa0, sa1);
    mac_words(haveDa ? dmaj : o->major, haveDa ? dmin : o->minor, da0, da1);
    W(*s, TXHDR1 + 0x10, sa0); W(*s, TXHDR1 + 0x14, sa1); W(*s, TXHDR1 + 0x18, da0); W(*s, TXHDR1 + 0x1c, da1);
    W(*s, TXQ2 + 0x84, 1);
  }
  for (auto& L : links) for (Side* s : {&L->A, &L->B}) W(*s, TXQ2 + 0x00, 1);
  if (!g_go) {
    printf("\nDry run: would then run smoke, latency (%u samples), and bursts N=%u W=%u for sizes", latN, N, Wn);
    for (uint32_t s : sizes) printf(" %u", s);
    printf(" on %zu link(s), then %s.\nRe-run with --go to perform it.\n", links.size(), keep ? "leave queue 2 configured" : "restore");
    return 0;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  bool pass = true; uint32_t tag = 1;
  printf("\n--- phase 1: smoke (64 B, 1 slot, each direction, every link) ---\n");
  for (auto& L : links) for (int dir = 0; dir < 2; ++dir) {
    Side& tx = dir ? L->B : L->A; Side& rx = dir ? L->A : L->B;
    double us = 0; uint32_t good = tagged_window(tx, rx, 64, 1, tag++, &us);
    printf("  %s -> %s: %s (rx-seq advance seen after %.0f us)\n", tx.name.c_str(), rx.name.c_str(), good == 1 ? "OK" : "FAIL", us);
    pass = pass && good == 1;
  }

  if (latN) {
    printf("\n--- phase 2: latency, first link, each direction (no other traffic) ---\n");
    latency(links[0]->A, links[0]->B, latN);
    latency(links[0]->B, links[0]->A, latN);
  }

  printf("\n--- phase 3: bursts (all %zu link(s) x 2 directions, round-robin issue) ---\n", links.size());
  for (uint32_t S : sizes) {
    std::vector<Dir> dirs;
    for (auto& L : links) { dirs.push_back({&L->A, &L->B}); dirs.push_back({&L->B, &L->A}); }
    bool ok = burst(dirs, S, N, Wn, tag++);
    pass = pass && ok;
    printf("  -- phase 4: fresh tagged window after burst, S=%u --\n", S);
    for (auto& L : links) for (int dir = 0; dir < 2; ++dir) {
      Side& tx = dir ? L->B : L->A; Side& rx = dir ? L->A : L->B;
      uint32_t good = tagged_window(tx, rx, S, std::min<uint32_t>(Wn, 4), tag++, nullptr);
      if (good != std::min<uint32_t>(Wn, 4)) pass = false;
    }
    if (g_bad) { printf("  stopping early: %d write problems\n", g_bad); break; }
    if (!ok && S >= 4096) { printf("  (4096 B is the split test; failure here only means splitting is unusable as-is)\n"); }
  }

  printf("\n--- final queue-2 state ---\n");
  for (auto& L : links) for (Side* s : {&L->A, &L->B})
    printf("  %s TXQ2 ctrl=%x xfer=%u | RXQ2 ctrl=%x seq=%u rseq=%u drops=%u\n", s->name.c_str(), R(*s, TXQ2), R(*s, TXQ2 + 0x30), R(*s, RXQ2),
           R(*s, RXQ2 + 0x40), R(*s, RXQ2 + 0x44), R(*s, RXQ2 + 0x4c));
  if (!keep) { printf("\n--- restore ---\n"); for (auto& L : links) { restore(L->A); restore(L->B); } }
  else printf("\n--keep: queue 2 / TXHDR[1] left configured\n");
  printf("\nRESULT: %s (%d write/readback problems)\n", (pass && !g_bad) ? "PASS" : "FAIL", g_bad);
  return (pass && !g_bad) ? 0 : 1;
}
