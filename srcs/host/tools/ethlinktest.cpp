// ethlinktest.cpp -- WRITES (small, allowlisted) to ONE pair of Blackhole ETH tiles on two cards,
// to test a custom TT-link path driven from the host over PCIe, before investing in an E1 image.
//
// Hypothesis under test (from ethprobe12 data): base FW configures only TX/RX queue 0 (broadcast DA);
// the RX classifier routes UNICAST data frames to RX queue 2 (MAC_RX_ADDR_ROUTING=0xA00), and queues 1/2
// plus TX header-table entries 1..9 are idle.  So: header entry 1 = {SA=own MAC, DA=peer MAC},
// TXQ2 in TT-link mode (ctrl=1), RXQ2 in TT-link RX mode (ctrl=2), send CMD=2 L1-write packets.
// Queue 2 is used on both ends so ack traffic pairs TXQ2<->RXQ2 (TX#n pairs with RX#n per the docs).
//
// SAFETY: default is a DRY RUN (reads only).  --go enables writes.  Every write is checked against an
// allowlist (write_ok): queue-2 regs, TXHDR[1], and two small L1 scratch ranges (0x20000, 0x30000).
// Nothing touches queue 0, E0/E1 reset, the classifier, or the MAC/PCS.  Both tiles must be Up and be each
// others' firmware-reported peers.  Queue state is zeroed again afterwards unless --keep.
// UNKNOWNS: whether the MAC drops unicast DAs, whether TX seq state is per-queue, undocumented TXQ regs
// (+0x68/+0x6c) that differ on queue 0.  A FAIL here is information, not damage (moderate confidence).
//
// Build: g++ -std=c++17 -O1 -Wall -Wextra -Werror ethlinktest.cpp -o ethlinktest
// Run:   ./ethlinktest --a-dev 0 --b-dev 1 --tile 6            # dry run
//        ./ethlinktest --a-dev 0 --b-dev 1 --tile 6 --go       # do it
//
// ioctl plumbing is copied from ethprobe12.cpp.
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
constexpr uint32_t SRC_L1 = 0x20000, DST_L1 = 0x30000, NBYTES = 64;
constexpr uint32_t RXBUF_L1 = 0x7E000;  // zero page; RXQ0 uses 0x7F000

// ---- device port: one per card; ETH tiles are at Y=1 ----
struct Port {
  virtual ~Port() {}
  virtual bool rd32(unsigned x, uint32_t a, uint32_t& v) = 0;
  virtual bool wr32(unsigned x, uint32_t a, uint32_t v) = 0;
};

struct HwPort : Port {
  int fd = -1; uint32_t tlbId = 0; bool haveTlb = false;
  volatile uint8_t* win = nullptr;
  unsigned cx = ~0u; uint64_t cbase = ~0ull;
  bool open_dev(const std::string& path, std::string& err) {
    fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) { err = "open " + path + ": " + strerror(errno); return false; }
    tt_get_device_info di; memset(&di, 0, sizeof di); di.in.output_size_bytes = sizeof di.out;
    if (ioctl(fd, TT_IOCTL_GET_DEVICE_INFO, &di) < 0) { err = "GET_DEVICE_INFO failed"; return false; }
    if (di.out.vendor_id != PCI_VENDOR_TENSTORRENT || di.out.device_id != PCI_DEVICE_BLACKHOLE) { err = "not a Blackhole"; return false; }
    tt_allocate_tlb at; memset(&at, 0, sizeof at); at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) { err = std::string("ALLOCATE_TLB: ") + strerror(errno); return false; }
    tlbId = at.out.id; haveTlb = true;
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) { err = std::string("mmap: ") + strerror(errno); return false; }
    win = (volatile uint8_t*)p; return true;
  }
  ~HwPort() override {
    if (win) munmap((void*)win, TLB_SIZE);
    if (fd >= 0 && haveTlb) { tt_free_tlb ft; memset(&ft, 0, sizeof ft); ft.in.id = tlbId; (void)ioctl(fd, TT_IOCTL_FREE_TLB, &ft); }
    if (fd >= 0) ::close(fd);
  }
  volatile uint32_t* map(unsigned x, uint32_t a) {
    if (a & 3u) return nullptr;
    uint64_t base = (uint64_t)a & ~TLB_MASK; uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    if (x != cx || base != cbase) {
      tt_configure_tlb c; memset(&c, 0, sizeof c);
      c.in.id = tlbId; c.in.config.addr = base;
      c.in.config.x_start = x; c.in.config.x_end = x; c.in.config.y_start = 1; c.in.config.y_end = 1;
      c.in.config.noc = 0; c.in.config.mcast = 0;
      if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) return nullptr;
      cx = x; cbase = base;
    }
    return (volatile uint32_t*)(win + off);
  }
  bool rd32(unsigned x, uint32_t a, uint32_t& v) override { volatile uint32_t* p = map(x, a); if (!p) return false; v = *p; return true; }
  bool wr32(unsigned x, uint32_t a, uint32_t v) override { volatile uint32_t* p = map(x, a); if (!p) return false; *p = v; return true; }
};

// ---- MOCK: a tiny fake of two cards, enough to exercise this tool's logic ----
struct Fabric {
  std::map<uint64_t, uint32_t> m;   // key = dev<<48 | x<<40 | addr
  static uint64_t key(int d, unsigned x, uint32_t a) { return ((uint64_t)d << 48) | ((uint64_t)x << 40) | a; }
  uint32_t get(int d, unsigned x, uint32_t a) { auto it = m.find(key(d, x, a)); return it == m.end() ? 0 : it->second; }
  void put(int d, unsigned x, uint32_t a, uint32_t v) { m[key(d, x, a)] = v; }
  static uint32_t minor(int d, unsigned x) { return 0x100000u * (uint32_t)(d + 1) + x; }
  void init(unsigned x) {
    for (int d = 0; d < 2; ++d) {
      put(d, x, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, 0x00020000u | 10);
      put(d, x, ETH_BOOT_RESULTS_ADDR + 4, 1);
      uint32_t r = ETH_BOOT_RESULTS_ADDR + 4 * 240;
      put(d, x, r + 12, 0x208c47); put(d, x, r + 16, minor(d, x)); put(d, x, r + 28, 1);
      r = ETH_BOOT_RESULTS_ADDR + 4 * 248;
      put(d, x, r + 12, 0x208c47); put(d, x, r + 16, minor(d ^ 1, x)); put(d, x, r + 28, 1);
    }
  }
  static uint32_t macw0(uint32_t major, uint32_t minor) {
    return (minor & 0xff) | (((minor >> 8) & 0xff) << 8) | (((minor >> 16) & 0xff) << 16) | ((major & 0xff) << 24);
  }
  void cmd(int d, unsigned x) {
    int p = d ^ 1;
    bool txok = get(d, x, TXQ2) & 1, rxok = (get(p, x, RXQ2) & 2) != 0;
    bool da = get(d, x, TXHDR1 + 0x18) == macw0(0x208c47, minor(p, x));   // DA word0 must be peer MAC
    uint32_t src = get(d, x, TXQ2 + 0x14), n = get(d, x, TXQ2 + 0x18), dst = get(d, x, TXQ2 + 0x1c);
    put(d, x, TXQ2 + 0x30, get(d, x, TXQ2 + 0x30) + 1);
    if (txok && rxok && da && ((get(d, x, TXQ2 + 0x80) >> 8) & 15) == 1) {
      for (uint32_t i = 0; i < n; i += 4) put(p, x, dst + i, get(d, x, src + i));
      put(p, x, RXQ2 + 0x24, get(p, x, RXQ2 + 0x24) + 1);
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
  auto in = [&](uint32_t lo, uint32_t hi) { return a >= lo && a < hi; };
  if (a == TXQ2 + 0x04) return v == 2;                                   // CMD: L1-write packet only
  if (a == TXQ2 + 0x00) return v == 0 || v == 1;                         // ctrl: off / TT-link
  if (a == TXQ2 + 0x14 || a == TXQ2 + 0x18 || a == TXQ2 + 0x1c || a == TXQ2 + 0x80 || a == TXQ2 + 0x84) return true;
  if (a == RXQ2 + 0x00) return v == 0 || v == 2;                         // ctrl: off / TT-link
  if (a == RXQ2 + 0x0c || a == RXQ2 + 0x10 || a == RXQ2 + 0x18) return true;
  if (a == TXHDR1 + 0x10 || a == TXHDR1 + 0x14 || a == TXHDR1 + 0x18 || a == TXHDR1 + 0x1c) return true;
  return in(SRC_L1, SRC_L1 + 0x100) || in(DST_L1, DST_L1 + 0x100);
}

struct Side {
  Port* p; int dev; unsigned x; const char* name;
  uint32_t major = 0, minor = 0;           // own MAC
  uint32_t pmajor = 0, pminor = 0;         // MAC the firmware says the peer has
};

bool g_go = false;
unsigned g_wait_ms = 50;
int g_bad = 0;

bool W(Side& s, uint32_t a, uint32_t v, bool verify = true) {
  if (!write_ok(a, v)) { fprintf(stderr, "INTERNAL: write 0x%08x=0x%x not allowlisted; aborting\n", a, v); exit(3); }
  printf("  %s W dev%d X=%-2u 0x%08x <= 0x%08x%s\n", s.name, s.dev, s.x, a, v, g_go ? "" : "   (dry-run: NOT written)");
  if (!g_go) return true;
  if (!s.p->wr32(s.x, a, v)) { printf("    write failed\n"); ++g_bad; return false; }
  if (verify && a != TXQ2 + 4) {
    uint32_t b = 0;
    if (!s.p->rd32(s.x, a, b) || b != v) { printf("    READBACK MISMATCH: got 0x%08x\n", b); ++g_bad; return false; }
  }
  return true;
}
uint32_t R(Side& s, uint32_t a) { uint32_t v = 0; if (!s.p->rd32(s.x, a, v)) { v = 0xDEADDEAD; } return v; }

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

struct QSnap { uint32_t v[0x54 / 4]; };
void snap_q(Side& s, uint32_t base, QSnap& q) { for (unsigned i = 0; i < 0x54 / 4; ++i) q.v[i] = R(s, base + 4 * i); }

// Validate a side before touching it.  Returns false if anything is off.
bool validate(Side& s, bool force) {
  uint32_t ep = R(s, NIU0_ADDR + NOC_ENDPOINT_ID_OFF), cfg = R(s, NIU0_ADDR + NIU_CFG_0_OFF);
  if ((uint16_t)(ep >> 8) != 0x0200) { printf("%s: X=%u is not an ETH tile (ep=0x%08x)\n", s.name, s.x, ep); return false; }
  if (cfg & NIU_CFG_0_HARVESTED) { printf("%s: tile harvested\n", s.name); return false; }
  uint32_t port = R(s, ETH_BOOT_RESULTS_ADDR + 4);
  if (port != 1) { printf("%s: port status %u (need 1=Up)\n", s.name, port); return false; }
  uint32_t r = ETH_BOOT_RESULTS_ADDR + 4 * 240;
  if (R(s, r + 28) != 1 || R(s, r + 32 + 28) != 1) { printf("%s: chip-info records not valid\n", s.name); return false; }
  s.major = R(s, r + 12); s.minor = R(s, r + 16);
  s.pmajor = R(s, r + 32 + 12); s.pminor = R(s, r + 32 + 16);
  bool pristine = R(s, TXQ2) == 0 && R(s, RXQ2) == 0 && R(s, TXHDR1 + 0x10) == 0 && R(s, TXHDR1 + 0x14) == 0 &&
                  R(s, TXHDR1 + 0x18) == 0 && R(s, TXHDR1 + 0x1c) == 0 && R(s, TXQ2 + 0x80) == 0 && R(s, TXQ2 + 0x84) == 0;
  printf("%s: dev%d X=%u own MAC %s, FW says peer %s, queue-2/hdr-1 pristine=%s\n", s.name, s.dev, s.x,
         macstr(s.major, s.minor).c_str(), macstr(s.pmajor, s.pminor).c_str(), pristine ? "yes" : "NO");
  if (!pristine && !force) { printf("  refusing: queue-2/TXHDR[1] already in use (use --force to override)\n"); return false; }
  return true;
}

void report(Side& tx, Side& rx, const QSnap& tx0, const QSnap& rx0, bool& pass) {
  QSnap t1, r1; snap_q(tx, TXQ2, t1); snap_q(rx, RXQ2, r1);
  printf("  counters (before -> after):\n");
  printf("    %s TXQ2 xfer_cnt(+30) %u->%u  pkt_start(+34) %u->%u  pkt_end(+3c) %u->%u  word_cnt(+40) %u->%u\n", tx.name,
         tx0.v[0x30 / 4], t1.v[0x30 / 4], tx0.v[0x34 / 4], t1.v[0x34 / 4], tx0.v[0x3c / 4], t1.v[0x3c / 4], tx0.v[0x40 / 4], t1.v[0x40 / 4]);
  printf("    %s RXQ2 bytes(+04) %u->%u  words(+14) %u->%u  pkt_start(+24) %u->%u  pkt_end(+28) %u->%u  rx_seq(+40) %u->%u  drops(+4c) %u->%u\n", rx.name,
         rx0.v[1], r1.v[1], rx0.v[5], r1.v[5], rx0.v[9], r1.v[9], rx0.v[10], r1.v[10], rx0.v[0x40 / 4], r1.v[0x40 / 4],
         rx0.v[0x4c / 4], r1.v[0x4c / 4]);
  unsigned good = 0;
  for (uint32_t i = 0; i < NBYTES; i += 4) good += (R(rx, DST_L1 + i) == (0xC0DE0000u | ((uint32_t)tx.dev << 12) | (i / 4)));
  printf("  destination L1 @0x%x on %s: %u/%u words match pattern", DST_L1, rx.name, good, NBYTES / 4);
  if (good != NBYTES / 4) printf("   first words: %08x %08x %08x %08x", R(rx, DST_L1), R(rx, DST_L1 + 4), R(rx, DST_L1 + 8), R(rx, DST_L1 + 12));
  printf("\n");
  pass = pass && (good == NBYTES / 4);
}

void send(Side& tx, Side& rx, bool& pass) {
  printf("\n--- send %s -> %s: %u bytes, L1 0x%x -> 0x%x via TXQ2 ---\n", tx.name, rx.name, NBYTES, SRC_L1, DST_L1);
  for (uint32_t i = 0; i < NBYTES; i += 4) {
    W(tx, SRC_L1 + i, 0xC0DE0000u | ((uint32_t)tx.dev << 12) | (i / 4), true);
    W(rx, DST_L1 + i, 0xA5A5A5A5u, true);
  }
  QSnap t0, r0; snap_q(tx, TXQ2, t0); snap_q(rx, RXQ2, r0);
  W(tx, TXQ2 + 0x14, SRC_L1); W(tx, TXQ2 + 0x18, NBYTES); W(tx, TXQ2 + 0x1c, DST_L1);
  W(tx, TXQ2 + 0x80, 1u << 8);            // L1-write packets use TX header table entry 1
  if (!g_go) return;
  for (int i = 0; i < 1000 && (R(tx, TXQ2 + 0x08) & 0x10000); ++i) std::this_thread::sleep_for(std::chrono::microseconds(10));
  W(tx, TXQ2 + 0x04, 2, false);          // CMD = L1 write packet
  (void)R(tx, TXQ2 + 0x04);               // read back CMD before STATUS (docs: ordering)
  int polls = 0;
  while ((R(tx, TXQ2 + 0x08) & 0x10000) && polls < 1000) { ++polls; std::this_thread::sleep_for(std::chrono::microseconds(100)); }
  printf("  CMD accepted after %d status polls (status=0x%08x)\n", polls, R(tx, TXQ2 + 0x08));
  std::this_thread::sleep_for(std::chrono::milliseconds(g_wait_ms));
  report(tx, rx, t0, r0, pass);
}

void restore(Side& s) {
  printf("  restoring %s (dev%d X=%u)\n", s.name, s.dev, s.x);
  W(s, TXQ2 + 0x00, 0);
  W(s, RXQ2 + 0x00, 0);
  W(s, TXQ2 + 0x80, 0); W(s, TXQ2 + 0x84, 0);
  W(s, TXHDR1 + 0x10, 0); W(s, TXHDR1 + 0x14, 0); W(s, TXHDR1 + 0x18, 0); W(s, TXHDR1 + 0x1c, 0);
}

bool parse_u(const char* s, unsigned& o) { char* e = nullptr; unsigned long v = strtoul(s, &e, 0); if (e == s || *e) return false; o = (unsigned)v; return true; }
bool parse_mac(const char* s, uint32_t& major, uint32_t& minor) {
  unsigned o[6]; if (sscanf(s, "%x:%x:%x:%x:%x:%x", &o[0], &o[1], &o[2], &o[3], &o[4], &o[5]) != 6) return false;
  major = (o[0] << 16) | (o[1] << 8) | o[2]; minor = (o[3] << 16) | (o[4] << 8) | o[5]; return true;
}

void usage() {
  fprintf(stderr,
          "ethlinktest -- WRITES to two Blackhole ETH tiles (one link pair) to test a custom TT-link path.\n"
          "  --a-dev N --b-dev M --tile X   the two cards and the ETH tile's NoC0 X (same X both ends; E10=6)\n"
          "  --go                           actually write (default is a dry run that only reads)\n"
          "  --keep                         leave queue-2/TXHDR[1] configured afterwards (default: restore)\n"
          "  --force                        proceed even if queue 2 / TXHDR[1] are not pristine\n"
          "  --da MAC                       override destination MAC (must be unicast; default = peer's FW-reported MAC)\n"
          "  --wait-ms N                    settle time after CMD (default 50)\n"
          "  --mock                         exercise the logic against a FAKE two-card model\n");
}

}  // namespace

int main(int argc, char** argv) {
  int adev = -1, bdev = -1; unsigned x = 0; bool keep = false, force = false, mock = false, haveTile = false;
  bool haveDa = false; uint32_t dmaj = 0, dmin = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i]; unsigned v = 0;
    if (a == "--a-dev" && i + 1 < argc && parse_u(argv[i + 1], v)) { adev = (int)v; ++i; }
    else if (a == "--b-dev" && i + 1 < argc && parse_u(argv[i + 1], v)) { bdev = (int)v; ++i; }
    else if (a == "--tile" && i + 1 < argc && parse_u(argv[i + 1], v)) { x = v; haveTile = true; ++i; }
    else if (a == "--wait-ms" && i + 1 < argc && parse_u(argv[i + 1], v)) { g_wait_ms = v; ++i; }
    else if (a == "--da" && i + 1 < argc && parse_mac(argv[i + 1], dmaj, dmin)) { haveDa = true; ++i; }
    else if (a == "--go") g_go = true;
    else if (a == "--keep") keep = true;
    else if (a == "--force") force = true;
    else if (a == "--mock") mock = true;
    else { usage(); return 2; }
  }
  if (adev < 0 || bdev < 0 || adev == bdev || !haveTile) { usage(); return 2; }
  if (haveDa && ((dmaj >> 16) & 1)) { fprintf(stderr, "--da must be unicast (LSB of first octet clear): multicast/broadcast would route to RXQ0 (base FW)\n"); return 2; }

  Fabric fab; std::unique_ptr<Port> pa, pb;
  if (mock) {
    printf("*** MOCK MODE: FAKE two-card model; nothing here is hardware ***\n");
    fab.init(x); pa.reset(new MockPort(fab, 0)); pb.reset(new MockPort(fab, 1)); adev = 0; bdev = 1;
  } else {
    for (int k = 0; k < 2; ++k) {
      int d = k ? bdev : adev; HwPort* hp = new HwPort(); std::string err;
      (k ? pb : pa).reset(hp);
      if (!hp->open_dev("/dev/tenstorrent/" + std::to_string(d), err)) { fprintf(stderr, "dev%d: %s\n", d, err.c_str()); return 1; }
    }
  }
  Side A{pa.get(), adev, x, "A"}, B{pb.get(), bdev, x, "B"};
  printf("%s\n", g_go ? "MODE: --go (WILL WRITE)" : "MODE: dry run (reads only; writes are printed, not issued)");
  if (!validate(A, force) || !validate(B, force)) return 1;
  if (A.pmajor != B.major || A.pminor != B.minor || B.pmajor != A.major || B.pminor != A.minor) {
    printf("link check FAILED: firmware peer records do not name each other (wrong card pair or tile?)\n"); return 1;
  }
  printf("link check OK: A and B are each other's FW-reported peers\n");

  uint32_t daA_maj = haveDa ? dmaj : B.major, daA_min = haveDa ? dmin : B.minor;   // A's DA
  uint32_t daB_maj = haveDa ? dmaj : A.major, daB_min = haveDa ? dmin : A.minor;   // B's DA
  uint32_t sa0, sa1, da0, da1;

  printf("\n--- configure (RX queues first, then headers, then TX queues) ---\n");
  for (Side* s : {&A, &B}) { W(*s, RXQ2 + 0x0c, RXBUF_L1 >> 4); W(*s, RXQ2 + 0x10, 0x100); W(*s, RXQ2 + 0x18, 0xe); W(*s, RXQ2 + 0x00, 2); }
  for (Side* s : {&A, &B}) {
    bool isA = (s == &A);
    mac_words(s->major, s->minor, sa0, sa1);
    mac_words(isA ? daA_maj : daB_maj, isA ? daA_min : daB_min, da0, da1);
    W(*s, TXHDR1 + 0x10, sa0); W(*s, TXHDR1 + 0x14, sa1); W(*s, TXHDR1 + 0x18, da0); W(*s, TXHDR1 + 0x1c, da1);
    W(*s, TXQ2 + 0x84, 1);   // seq-update (ack) packets also use entry 1
  }
  for (Side* s : {&A, &B}) W(*s, TXQ2 + 0x00, 1);
  if (g_go) std::this_thread::sleep_for(std::chrono::milliseconds(20));

  bool pass = true;
  send(A, B, pass);
  send(B, A, pass);

  if (g_go) {
    printf("\n--- queue-2 state after test ---\n");
    for (Side* s : {&A, &B}) {
      QSnap t, r; snap_q(*s, TXQ2, t); snap_q(*s, RXQ2, r);
      printf("  %s TXQ2: ctrl=%x xfer=%u start=%u end=%u words=%u | RXQ2: ctrl=%x pkts=%u/%u seq=%u rseq=%u drops=%u\n", s->name, t.v[0], t.v[0x30 / 4],
             t.v[0x34 / 4], t.v[0x3c / 4], t.v[0x40 / 4], r.v[0], r.v[9], r.v[10], r.v[0x40 / 4], r.v[0x44 / 4], r.v[0x4c / 4]);
    }
  }
  if (g_go && !keep) { printf("\n--- restore ---\n"); restore(A); restore(B); }
  else if (g_go) printf("\n--keep: queue 2 / TXHDR[1] left configured on both ends\n");
  if (g_go) printf("\nRESULT: %s (%d write/readback problems)\n", (pass && !g_bad) ? "PASS" : "FAIL", g_bad);
  else printf("\nDry run complete; re-run with --go to perform the test.\n");
  return (g_go && !(pass && !g_bad)) ? 1 : 0;
}
