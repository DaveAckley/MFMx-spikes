// ethprobe11.cpp -- READ-ONLY probe of Blackhole Ethernet tiles, tt-kmd only.
// (version 11: adds topology decode from firmware peer records, a probe of
//  the Wormhole-documented 0x11000 queue block, and --save L1 snapshots.)
//
// Purpose: step 1 of the 'eth-seam' project.  Find out what the base
// firmware on each ETH tile is doing, so we can decide how to move MFMx
// packets card-to-card over the QSFP-DD cables without PCIe.  Specifically:
//   * which ETH tiles exist / are harvested, and what their link state is
//   * is the ERISC base firmware alive (heartbeat ticking)?
//   * does the Wormhole-documented 'baseline data movement service' queue
//     block exist at L1 0x11000 on BH?  (L1 0x170 is NOT a pointer on BH;
//     the first run showed varying per-tile data there.)
//   * peer/chip-info snippets the firmware leaves in its boot-results block
//
// SAFETY: this program NEVER writes to the device (it does write host files
// when --save is given).  It allocates one 2 MiB
// TLB, configures it (via the tt-kmd CONFIGURE_TLB ioctl, like OurTLBs.cpp),
// and does 32-bit loads.  The only addresses it will read are ETH-tile L1
// below 512 KiB and ETH-tile register space at/above 0xFFB00000.  It only
// touches tiles whose NOC_ENDPOINT_ID says they are Ethernet tiles.
// It does consume one TLB per run; it frees it on exit.
//
// Field decoding (boot params/results layout, port/training status codes)
// follows tt-isa-documentation BlackholeA0/EthernetTile/Samples/ethdump/
// ethdump.c, Copyright 2025 Tenstorrent AI ULC, Apache-2.0.
//
// Build (no dependencies beyond libstdc++):
//   g++ -std=c++17 -O1 -Wall -Wextra -Werror ethprobe11.cpp -o ethprobe11
//
// Usage:
//   ./ethprobe11                   # probe every /dev/tenstorrent/N
//   ./ethprobe11 --device 2          # just one card
//   ./ethprobe11 --raw               # also hexdump boot params/results words
//   ./ethprobe11 --tile 4 --dump 0x7cc00 16   # read 16 words of ETH tile X=4
//   ./ethprobe11 --tile 3 --save snap       # save full 512 KiB L1 of ETH tile X=3
//                                           # on every card to snap-devN-E4-X3.bin
//   ./ethprobe11 --mock              # fake data (NOT hardware!) to test the tool
//
// Coordinates here are raw NoC0 X (1..7,10..16 at Y=1), as in ethdump.

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

// ---- Blackhole ETH tile addresses (from ethdump.c / tt-system-firmware)
constexpr uint32_t ETH_L1_SIZE            = 512u * 1024u;
constexpr uint32_t ETH_BOOT_PARAMS_ADDR   = 0x0007C000;
constexpr uint32_t ETH_BOOT_RESULTS_ADDR  = 0x0007CC00;
constexpr uint32_t ETH_HEARTBEAT_ADDR     = 0x0007CC70;  // == results[28]
constexpr uint32_t ETH_SVC_QUEUE_PTR_ADDR = 0x00000170;  // WORMHOLE doc; unverified on BH
constexpr uint32_t SOFT_RESET_ADDR        = 0xFFB121B0;
constexpr uint32_t NIU0_ADDR              = 0xFFB20000;
constexpr uint32_t NOC_ENDPOINT_ID_OFFSET = 0x048;
constexpr uint32_t NIU_CFG_0_OFFSET       = 0x100;
constexpr uint32_t NOC_ID_LOGICAL_OFFSET  = 0x148;
constexpr uint32_t NIU_CFG_0_HARVESTED    = 1u << 12;

// base-FW queue block layout (WORMHOLE doc: eth_base_firmware_queues_t)
constexpr uint32_t QB_SQ_OFFSET = 0x80;    // after uint64 latency_counter[16]
constexpr uint32_t QB_CQ_OFFSET = 0x200;   // sq(0xC0) + reserved(0xC0) after 0x80

// ---- Reader abstraction so --mock can exercise the same code paths ----
struct Reader {
  virtual ~Reader() {}
  virtual bool rd32(unsigned x, unsigned y, uint32_t addr, uint32_t& out) = 0;
};

struct HwReader : Reader {
  int fd = -1;
  uint32_t tlbId = 0;
  bool haveTlb = false;
  volatile uint8_t* win = nullptr;
  unsigned cx = ~0u, cy = ~0u;
  uint64_t cbase = ~0ull;

  bool open_dev(const std::string& path, std::string& err, tt_get_device_info_out* infoOut) {
    fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) { err = "open " + path + ": " + strerror(errno); return false; }
    tt_get_device_info di;
    memset(&di, 0, sizeof di);
    di.in.output_size_bytes = sizeof di.out;
    if (ioctl(fd, TT_IOCTL_GET_DEVICE_INFO, &di) < 0) { err = "GET_DEVICE_INFO failed"; return false; }
    if (di.out.vendor_id != PCI_VENDOR_TENSTORRENT || di.out.device_id != PCI_DEVICE_BLACKHOLE) {
      err = "not a Tenstorrent Blackhole device"; return false;
    }
    if (infoOut) *infoOut = di.out;
    tt_allocate_tlb at;
    memset(&at, 0, sizeof at);
    at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) {
      err = std::string("ALLOCATE_TLB failed: ") + strerror(errno) + " (tt-kmd too old, or all TLBs in use?)";
      return false;
    }
    tlbId = at.out.id; haveTlb = true;
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) { err = std::string("mmap TLB failed: ") + strerror(errno); return false; }
    win = (volatile uint8_t*)p;
    return true;
  }

  ~HwReader() override {
    if (win) munmap((void*)win, TLB_SIZE);
    if (fd >= 0 && haveTlb) {
      tt_free_tlb ft; memset(&ft, 0, sizeof ft); ft.in.id = tlbId;
      (void)ioctl(fd, TT_IOCTL_FREE_TLB, &ft);
    }
    if (fd >= 0) ::close(fd);
  }

  bool rd32(unsigned x, unsigned y, uint32_t addr, uint32_t& out) override {
    if (addr & 3u) return false;
    uint64_t base = (uint64_t)addr & ~TLB_MASK;
    uint32_t off = (uint32_t)((uint64_t)addr & TLB_MASK);
    if (x != cx || y != cy || base != cbase) {
      tt_configure_tlb c;
      memset(&c, 0, sizeof c);
      c.in.id = tlbId;
      c.in.config.addr = base;
      c.in.config.x_end = x; c.in.config.y_end = y;      // unicast: start==end
      c.in.config.x_start = x; c.in.config.y_start = y;
      c.in.config.noc = 0;                               // NoC0, like OurTLBs.cpp
      c.in.config.mcast = 0;
      // ordering left 0, as OurTLBs.cpp does; we only do reads.
      if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) return false;
      cx = x; cy = y; cbase = base;
    }
    out = *(volatile const uint32_t*)(win + off);
    return true;
  }
};


// ---- records collected per device, for the cross-card topology pass ----
struct ChipInfoRec {          // 8-word record the base FW leaves at boot_results[240..255]
  bool valid = false;         // word 7 == 1
  uint32_t w0 = 0, w1 = 0, asic = 0, macMajor = 0, macMinor = 0;
};

struct TileRec {
  unsigned ethIdx = 0, x = 0;
  bool harvested = false;
  uint32_t port = 0, train = 0;
  uint32_t hb1 = 0;
  ChipInfoRec local, peer;
  char mac[24] = "N/A";      // actual (from local record) or firmware-derived
};

struct DevRec {
  int idx = -1;
  std::string pci;
  std::vector<TileRec> tiles;
};

std::string macstr(uint32_t major, uint32_t minor) {
  char b[24];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned)((major >> 16) & 255), (unsigned)((major >> 8) & 255),
           (unsigned)(major & 255), (unsigned)((minor >> 16) & 255), (unsigned)((minor >> 8) & 255), (unsigned)(minor & 255));
  return b;
}

// ---- Fake device for testing the tool's logic/format.  NOT HARDWARE. ----
// Models four cards in a ring-ish layout so the topology code has something to
// chew on.  Every number here is invented by this program.
struct MockReader : Reader {
  int dev;
  explicit MockReader(int d) : dev(d) {}
  static constexpr int NETH = 8;
  static int ethIdForChan(int c) { static const int ids[NETH] = {4, 6, 7, 9, 10, 11, 12, 13}; return ids[c]; }
  static int chanOfEth(int e) { for (int c = 0; c < NETH; ++c) if (ethIdForChan(c) == e) return c; return -1; }
  static uint32_t macMinorBase(int d) { static const uint32_t m[4] = {0x0563f4, 0x09bd48, 0x0ad2d4, 0x0b4b70}; return m[d & 3]; }
  static uint32_t asicOf(int d) { static const uint32_t a[4] = {0x3191113f, 0x3191117c, 0x31911168, 0x31911145}; return a[d & 3]; }
  static int peerCard(int d, int eth) { return eth >= 10 ? (d ^ 1) : (d ^ 2); }
  static bool linkUp(int d, int eth) { return !((d == 1 || d == 3) && (eth == 6 || eth == 7)); }
  static int eth_index_for_x(unsigned x) {
    static const int map_lo[8] = {-1, 0, 2, 4, 6, 8, 10, 12};   // x=1..7
    static const int map_hi[8] = {13, 11, 9, 7, 5, 3, 1, -1};   // x=10..16
    if (x >= 1 && x <= 7) return map_lo[x];
    if (x >= 10 && x <= 16) return map_hi[x - 10];
    return -1;
  }
  void chipinfo(int d, int eth, uint32_t* w) const {  // 8 words
    int c = chanOfEth(eth);
    w[0] = ((uint32_t)c << 24) | ((uint32_t)eth << 16) | 2u;
    w[1] = 0x412; w[2] = asicOf(d); w[3] = 0x208c47; w[4] = macMinorBase(d) + (uint32_t)c;
    w[5] = 0; w[6] = 0; w[7] = 1;
  }
  bool rd32(unsigned x, unsigned y, uint32_t addr, uint32_t& out) override {
    out = 0;
    int e = (y == 1) ? eth_index_for_x(x) : -1;
    if (e < 0) { out = 0x00010000u; return true; }  // some non-ETH endpoint id
    bool harvested = (e == 5 || e == 8);
    int chan = chanOfEth(e);
    bool noport = chan < 0;
    bool up = !noport && linkUp(dev, e);
    if (addr == NIU0_ADDR + NOC_ENDPOINT_ID_OFFSET) out = 0x00020000u | (uint32_t)e;
    else if (addr == NIU0_ADDR + NIU_CFG_0_OFFSET) out = harvested ? NIU_CFG_0_HARVESTED : 0;
    else if (addr == NIU0_ADDR + NOC_ID_LOGICAL_OFFSET) out = (25u << 6) | (20u + (unsigned)e);
    else if (addr == SOFT_RESET_ADDR) out = 0x00047000u;
    else if (addr >= ETH_BOOT_PARAMS_ADDR && addr < ETH_BOOT_PARAMS_ADDR + 0x400) {
      unsigned w = (addr - ETH_BOOT_PARAMS_ADDR) / 4;
      if (w == 0) out = 0x02023ed0u;
      else if (w == 36) out = 0x208c47;
      else if (w == 37) out = macMinorBase(dev);
    } else if (addr >= ETH_BOOT_RESULTS_ADDR && addr < ETH_BOOT_RESULTS_ADDR + 0x400) {
      unsigned w = (addr - ETH_BOOT_RESULTS_ADDR) / 4;
      if (w == 0) out = 0xc0dea000;
      else if (w == 1) out = noport ? 3 : (up ? 1 : 2);
      else if (w == 2) out = noport ? 1 : (up ? 2 : 6);
      else if (w == 3) out = 0x190;
      else if (w == 28) {
        using namespace std::chrono;
        out = 0xabcd0000u | (uint32_t)(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() & 0xffff);
      } else if (w == 32) out = noport ? 0 : 0xc0dea000;
      else if (w == 33) out = noport ? 0 : (eth_index_for_x(x) >= 10 ? 3u + (e >= 12) : 2u + (e >= 7));
      else if (w == 34) out = noport ? 0 : ((e == 4 || e == 9 || e == 11 || e == 13) ? 0x0fu : 0xf0u);
      else if (w >= 238 && w < 256 && up) {
        uint32_t rec[8];
        if (w == 238) out = 0x910;
        else if (w == 239) out = 0x10402;
        else if (w >= 240 && w < 248) { chipinfo(dev, e, rec); out = rec[w - 240]; }
        else if (w >= 248) { chipinfo(peerCard(dev, e), e, rec); out = rec[w - 248]; }
      }
    } else if (addr >= 0x11000 && addr < 0x11400) {
      out = 0;  // pretend the queue block exists but is idle
    }
    return true;
  }
};

// ---- helpers ----
bool is_eth_endpoint(uint32_t ep) { return (uint16_t)(ep >> 8) == 0x0200; }

bool l1_or_reg_ok(uint32_t addr) { return addr < ETH_L1_SIZE || addr >= 0xFFB00000u; }

bool rd_words(Reader& r, unsigned x, unsigned y, uint32_t addr, unsigned n, std::vector<uint32_t>& out) {
  out.assign(n, 0);
  for (unsigned i = 0; i < n; ++i)
    if (!r.rd32(x, y, addr + 4 * i, out[i])) return false;
  return true;
}

const char* port_status_str(uint32_t s) {
  static const char* m[] = {"Unknown", "Up", "Down", "No"};
  return s < 4 ? m[s] : "?";
}
const char* train_status_str(uint32_t s) {
  static const char* m[] = {"In Progress", "Skipped", "Complete", "Int Loopback", "Ext Loopback",
                            "Timeout (EQ)", "Timeout (AN)", "Timeout (CL)", "Timeout (BL)",
                            "Timeout (LU)", "Timeout (CI)"};
  return s < sizeof(m) / sizeof(*m) ? m[s] : "?";
}

struct Opts {
  int device = -1;       // -1 = all
  bool raw = false;
  bool mock = false;
  int tileX = -1;
  bool dump = false;
  uint32_t dumpAddr = 0;
  unsigned dumpWords = 0;
  unsigned hbMs = 100;
  std::string savePrefix;  // non-empty => save L1 snapshot(s)
};

void print_hexdump(Reader& r, unsigned x, unsigned y, uint32_t addr, unsigned nwords, const char* label) {
  std::vector<uint32_t> w;
  if (!rd_words(r, x, y, addr, nwords, w)) { printf("  %s: read failed\n", label); return; }
  printf("  %s @0x%08x (%u words):\n", label, addr, nwords);
  for (unsigned i = 0; i < nwords; i += 4) {
    printf("    +%04x:", 4 * i);
    for (unsigned j = i; j < i + 4 && j < nwords; ++j) printf(" %08x", w[j]);
    printf("\n");
  }
}

void print_queue(Reader& r, unsigned x, unsigned y, uint32_t qaddr, const char* name) {
  uint32_t c[5], wr = 0, rd = 0;
  bool ok = true;
  for (unsigned i = 0; i < 5; ++i) ok = ok && r.rd32(x, y, qaddr + 4 * i, c[i]);
  ok = ok && r.rd32(x, y, qaddr + 0x20, wr) && r.rd32(x, y, qaddr + 0x30, rd);
  if (!ok) { printf("    %s: read failed\n", name); return; }
  printf("    %s: wr_req=%u wr_resp=%u rd_req=%u rd_resp=%u err=%u wr_idx=%u rd_idx=%u\n", name,
         c[0], c[1], c[2], c[3], c[4], wr, rd);
}

ChipInfoRec parse_chipinfo(const std::vector<uint32_t>& res, unsigned base) {
  ChipInfoRec c;
  c.w0 = res[base + 0]; c.w1 = res[base + 1]; c.asic = res[base + 2];
  c.macMajor = res[base + 3]; c.macMinor = res[base + 4];
  c.valid = (res[base + 7] == 1);
  return c;
}

// The Wormhole docs place the base-FW service queue block at L1 0x11000.
// UNVERIFIED on Blackhole; we just look.
constexpr uint32_t WH_QUEUE_BLOCK_ADDR = 0x11000;

int probe_device(Reader& r, const Opts& o, const std::string& label, DevRec& dr) {
  printf("=== %s ===\n", label.c_str());
  printf("|Tile|NoC0 XY |Logical  |Port   |Training    |Serdes          |MAC              |HB(hex)  |\n");
  printf("|----|--------|---------|-------|------------|----------------|-----------------|---------|\n");

  const unsigned y = 1;
  for (unsigned x = 1; x <= 16; ++x) {
    if (x == 8 || x == 9) continue;
    if (o.tileX >= 0 && (unsigned)o.tileX != x) continue;
    uint32_t ep = 0, cfg = 0;
    if (!r.rd32(x, y, NIU0_ADDR + NOC_ENDPOINT_ID_OFFSET, ep)) {
      printf("|?   |X=%-2u,Y=%u|read of NOC_ENDPOINT_ID failed (ioctl error)\n", x, y);
      continue;
    }
    if (!is_eth_endpoint(ep)) {
      printf("|-   |X=%-2u,Y=%u|not an ETH tile (endpoint id 0x%08x)%s\n", x, y, ep,
             ep == 0xFFFFFFFFu ? "  [all-ones: PCIe read error or tile in reset?]" : "");
      continue;
    }
    TileRec t;
    t.x = x; t.ethIdx = ep & 0xff;
    (void)r.rd32(x, y, NIU0_ADDR + NIU_CFG_0_OFFSET, cfg);
    printf("|E%-3u|X=%-2u,Y=%u|", t.ethIdx, x, y);
    t.harvested = (cfg & NIU_CFG_0_HARVESTED) != 0;
    if (t.harvested) {
      printf("Harvested|N/A    |N/A         |N/A             |N/A              |N/A      |\n");
      dr.tiles.push_back(t);
      continue;
    }
    uint32_t lg = 0;
    (void)r.rd32(x, y, NIU0_ADDR + NOC_ID_LOGICAL_OFFSET, lg);
    lg &= 0xfff;
    printf("X=%-2u,Y=%-2u|", lg & 0x3f, lg >> 6);

    std::vector<uint32_t> res, par;
    if (!rd_words(r, x, y, ETH_BOOT_RESULTS_ADDR, 256, res) ||
        !rd_words(r, x, y, ETH_BOOT_PARAMS_ADDR, 64, par)) {
      printf("boot block read failed\n");
      dr.tiles.push_back(t);
      continue;
    }
    t.port = res[1];
    t.train = (res[1] == 0) ? 0 : res[2];
    printf("%-7s|", port_status_str(res[1]));
    if (t.train < 11) printf("%-12s|", train_status_str(t.train)); else printf("Status %-5u|", t.train);
    {
      char sd[48] = "N/A";
      if (res[32] >= 0xC0DE1000u && res[32] <= 0xC0DEFFFFu) {
        int n = snprintf(sd, sizeof sd, "#%u lanes", res[33]);
        const char* sep = " ";
        for (unsigned i = 0; i < 8 && n > 0 && (size_t)n < sizeof sd - 4; ++i)
          if (res[34] & (1u << i)) { n += snprintf(sd + n, sizeof sd - (size_t)n, "%s%u", sep, i); sep = ","; }
      }
      printf("%-16s|", sd);
    }
    t.local = parse_chipinfo(res, 240);
    t.peer = parse_chipinfo(res, 248);
    if (res[1] == 3) {
      printf("N/A              |");
    } else {
      uint32_t major, minor;
      if (t.local.valid) { major = t.local.macMajor; minor = t.local.macMinor; }
      else {
        uint32_t lid = (uint32_t)__builtin_popcount(par[0] & ((1u << (ep & 0x1f)) - 1));
        major = par[36]; minor = par[37] + lid;
      }
      std::string m = macstr(major, minor);
      snprintf(t.mac, sizeof t.mac, "%s", m.c_str());
      printf("%s|", t.mac);
    }
    t.hb1 = res[28];
    printf("%08x |\n", t.hb1);
    dr.tiles.push_back(t);

    if (o.raw) {
      print_hexdump(r, x, y, ETH_BOOT_PARAMS_ADDR, 48, "boot_params");
      print_hexdump(r, x, y, ETH_BOOT_RESULTS_ADDR, 64, "boot_results[0..63]");
      print_hexdump(r, x, y, ETH_BOOT_RESULTS_ADDR + 4 * 232, 24, "boot_results[232..255] (238/239 header?, 240..247 local chip-info, 248..255 peer chip-info)");
    }
  }

  // Heartbeat: second sample.
  std::this_thread::sleep_for(std::chrono::milliseconds(o.hbMs));
  printf("\nHeartbeat (u32 @0x%08x), second sample after %u ms:\n", ETH_HEARTBEAT_ADDR, o.hbMs);
  for (const TileRec& t : dr.tiles) {
    if (t.harvested || t.port == 3) continue;
    uint32_t hb2 = 0;
    if (!r.rd32(t.x, 1, ETH_HEARTBEAT_ADDR, hb2)) { printf("  E%-2u: read failed\n", t.ethIdx); continue; }
    printf("  E%-2u (X=%-2u): %08x -> %08x  %s\n", t.ethIdx, t.x, t.hb1, hb2,
           hb2 != t.hb1 ? "changing" : "UNCHANGED (stalled, or not a ticking counter)");
  }

  // Wormhole-documented queue block at 0x11000: just look at it.
  printf("\nL1 0x%05x (Wormhole-doc'd base-FW queue block; HYPOTHESIS for BH):\n", WH_QUEUE_BLOCK_ADDR);
  for (const TileRec& t : dr.tiles) {
    if (t.harvested || t.port == 3) continue;
    std::vector<uint32_t> w;
    if (!rd_words(r, t.x, 1, WH_QUEUE_BLOCK_ADDR, 256, w)) { printf("  E%-2u: read failed\n", t.ethIdx); continue; }
    unsigned nz = 0;
    for (uint32_t v : w) nz += (v != 0);
    printf("  E%-2u (X=%-2u): %u/256 nonzero words in first 1 KiB; w[0..3]=%08x %08x %08x %08x\n", t.ethIdx, t.x, nz,
           w[0], w[1], w[2], w[3]);
    if (nz) {
      print_queue(r, t.x, 1, WH_QUEUE_BLOCK_ADDR + QB_SQ_OFFSET, "sq (WH layout)");
      print_queue(r, t.x, 1, WH_QUEUE_BLOCK_ADDR + QB_CQ_OFFSET, "cq (WH layout)");
    }
  }

  printf("\nSoft-reset register 0xFFB121B0 (bit 11 = E0, bit 12 = E1; 1 = held in reset):\n");
  for (const TileRec& t : dr.tiles) {
    if (t.harvested) continue;
    uint32_t sr = 0;
    if (!r.rd32(t.x, 1, SOFT_RESET_ADDR, sr)) { printf("  E%-2u: read failed\n", t.ethIdx); continue; }
    printf("  E%-2u (X=%-2u): 0x%08x  E0=%s E1=%s\n", t.ethIdx, t.x, sr, (sr & 0x0800) ? "reset" : "running",
           (sr & 0x1000) ? "reset" : "running");
  }

  printf("\n");
  return 0;
}

// Cross-card pass: match each Up tile's peer MAC against every tile's own MAC.
void print_topology(const std::vector<DevRec>& devs) {
  printf("=== Topology (from base-FW peer chip-info records; decoded, not assumed) ===\n");
  struct Ref { int dev; unsigned eth; };
  std::vector<std::pair<std::string, Ref>> owners;
  for (const DevRec& d : devs)
    for (const TileRec& t : d.tiles)
      if (!t.harvested && t.port != 3 && strcmp(t.mac, "N/A") != 0) owners.push_back({t.mac, {d.idx, t.ethIdx}});
  auto find = [&](const std::string& m, Ref& out) {
    for (auto& p : owners) if (p.first == m) { out = p.second; return true; }
    return false;
  };

  struct Link { int da; unsigned ea; int db; unsigned eb; };
  std::vector<Link> links;
  unsigned asym = 0, unresolved = 0;
  std::vector<std::string> noPeer, downTiles;
  for (const DevRec& d : devs)
    for (const TileRec& t : d.tiles) {
      if (t.harvested || t.port == 3) continue;
      char who[40];
      snprintf(who, sizeof who, "card%d E%u", d.idx, t.ethIdx);
      if (t.port == 2) { downTiles.push_back(std::string(who) + " (" + train_status_str(t.train) + ")"); continue; }
      if (!t.peer.valid || !t.local.valid) { noPeer.push_back(who); continue; }
      std::string pm = macstr(t.peer.macMajor, t.peer.macMinor);
      Ref pr;
      if (!find(pm, pr)) {
        printf("  %s: peer MAC %s not found among probed cards (probe all cards to resolve)\n", who, pm.c_str());
        ++unresolved;
        continue;
      }
      // symmetry: the peer tile must name us back
      for (const DevRec& d2 : devs) {
        if (d2.idx != pr.dev) continue;
        for (const TileRec& t2 : d2.tiles)
          if (t2.ethIdx == pr.eth && t2.peer.valid && macstr(t2.peer.macMajor, t2.peer.macMinor) != t.mac) ++asym;
      }
      if (d.idx < pr.dev || (d.idx == pr.dev && t.ethIdx < pr.eth)) links.push_back({d.idx, t.ethIdx, pr.dev, pr.eth});
    }

  // group by card pair
  for (const DevRec& da : devs)
    for (const DevRec& db : devs) {
      if (db.idx <= da.idx) continue;
      std::string ends;
      unsigned n = 0;
      for (const Link& l : links)
        if (l.da == da.idx && l.db == db.idx) {
          char b[32]; snprintf(b, sizeof b, " E%u-E%u", l.ea, l.eb); ends += b; ++n;
        }
      if (n) printf("  card%d <-> card%d : %u link(s) x 400G:%s\n", da.idx, db.idx, n, ends.c_str());
    }
  if (links.empty()) printf("  (no resolved links)\n");
  if (!noPeer.empty()) { printf("  Up but no chip-info exchange record:"); for (auto& s : noPeer) printf(" [%s]", s.c_str()); printf("\n"); }
  if (!downTiles.empty()) { printf("  Down:"); for (auto& s : downTiles) printf(" [%s]", s.c_str()); printf("\n"); }
  printf("  asymmetric peer claims: %u   unresolved peers: %u\n\n", asym, unresolved);
}

// Full read-only snapshot of one ETH tile's 512 KiB L1 to a host file.
int save_tile(Reader& r, const Opts& o, int devIdx) {
  if (o.tileX < 0) { fprintf(stderr, "--save requires --tile X\n"); return 2; }
  uint32_t ep = 0, cfg = 0;
  if (!r.rd32((unsigned)o.tileX, 1, NIU0_ADDR + NOC_ENDPOINT_ID_OFFSET, ep) || !is_eth_endpoint(ep)) {
    fprintf(stderr, "card%d: tile X=%d,Y=1 is not an Ethernet tile (endpoint id 0x%08x); skipping\n", devIdx, o.tileX, ep);
    return 2;
  }
  (void)r.rd32((unsigned)o.tileX, 1, NIU0_ADDR + NIU_CFG_0_OFFSET, cfg);
  if (cfg & NIU_CFG_0_HARVESTED) { fprintf(stderr, "card%d: tile X=%d is harvested; skipping\n", devIdx, o.tileX); return 2; }
  std::vector<uint32_t> img(ETH_L1_SIZE / 4);
  auto t0 = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i < img.size(); ++i)
    if (!r.rd32((unsigned)o.tileX, 1, 4 * i, img[i])) { fprintf(stderr, "card%d: read failed at 0x%x\n", devIdx, 4 * i); return 1; }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  char fn[512];
  snprintf(fn, sizeof fn, "%s-dev%d-E%u-X%d.bin", o.savePrefix.c_str(), devIdx, ep & 0xff, o.tileX);
  FILE* f = fopen(fn, "wb");
  if (!f) { fprintf(stderr, "cannot create %s: %s\n", fn, strerror(errno)); return 1; }
  size_t wrote = fwrite(img.data(), 4, img.size(), f);
  fclose(f);
  if (wrote != img.size()) { fprintf(stderr, "short write to %s\n", fn); return 1; }
  printf("saved %s (%u bytes, little-endian words, L1 addr 0 first) in %.2f s\n", fn, (unsigned)(img.size() * 4), secs);
  return 0;
}

int dump_words(Reader& r, const Opts& o) {
  if (o.tileX < 0) { fprintf(stderr, "--dump requires --tile X\n"); return 2; }
  if (!l1_or_reg_ok(o.dumpAddr) || (o.dumpAddr & 3) || o.dumpWords == 0 || o.dumpWords > 4096 ||
      (o.dumpAddr < ETH_L1_SIZE && (uint64_t)o.dumpAddr + 4ull * o.dumpWords > ETH_L1_SIZE)) {
    fprintf(stderr, "refusing --dump: address must be 4-aligned, in ETH L1 (<0x80000, fully inside) or >=0xFFB00000; <=4096 words\n");
    return 2;
  }
  uint32_t ep = 0;
  if (!r.rd32((unsigned)o.tileX, 1, NIU0_ADDR + NOC_ENDPOINT_ID_OFFSET, ep) || !is_eth_endpoint(ep)) {
    fprintf(stderr, "tile X=%d,Y=1 is not an Ethernet tile (endpoint id 0x%08x); refusing\n", o.tileX, ep);
    return 2;
  }
  print_hexdump(r, (unsigned)o.tileX, 1, o.dumpAddr, o.dumpWords, "dump");
  return 0;
}

bool parse_u32(const char* s, uint32_t& out) {
  char* end = nullptr;
  errno = 0;
  unsigned long v = strtoul(s, &end, 0);
  if (errno || end == s || *end || v > 0xFFFFFFFFul) return false;
  out = (uint32_t)v;
  return true;
}

void usage() {
  fprintf(stderr,
          "ethprobe11 -- READ-ONLY Blackhole Ethernet tile probe (tt-kmd only)\n"
          "  --device N        probe only /dev/tenstorrent/N (default: all found)\n"
          "  --tile X          restrict to ETH tile at NoC0 X (Y=1)\n"
          "  --dump ADDR N     with --tile: hexdump N words at ADDR (ETH L1 or regs)\n"
          "  --save PREFIX     with --tile: save full 512 KiB L1 to PREFIX-devN-E<i>-X<x>.bin\n"
          "  --raw             also hexdump boot params/results per tile\n"
          "  --hb-ms N         heartbeat re-sample delay (default 100)\n"
          "  --mock            use FAKE data to test the tool (not hardware)\n");
}

}  // namespace

int main(int argc, char** argv) {
  Opts o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    uint32_t v = 0;
    if (a == "--device" && i + 1 < argc && parse_u32(argv[i + 1], v)) { o.device = (int)v; ++i; }
    else if (a == "--tile" && i + 1 < argc && parse_u32(argv[i + 1], v)) { o.tileX = (int)v; ++i; }
    else if (a == "--hb-ms" && i + 1 < argc && parse_u32(argv[i + 1], v)) { o.hbMs = v; ++i; }
    else if (a == "--save" && i + 1 < argc) { o.savePrefix = argv[i + 1]; ++i; }
    else if (a == "--dump" && i + 2 < argc && parse_u32(argv[i + 1], o.dumpAddr) && parse_u32(argv[i + 2], v)) {
      o.dump = true; o.dumpWords = v; i += 2;
    }
    else if (a == "--raw") o.raw = true;
    else if (a == "--mock") o.mock = true;
    else { usage(); return 2; }
  }

  std::vector<DevRec> devs;
  int rc = 0;

  if (o.mock) {
    printf("*** MOCK MODE: all values below are FAKE, generated by the tool itself ***\n");
    for (int n = 0; n < 4; ++n) {
      if (o.device >= 0 && n != o.device) continue;
      MockReader m(n);
      if (!o.savePrefix.empty()) { int r2 = save_tile(m, o, n); if (r2) rc = r2; continue; }
      if (o.dump) { rc = dump_words(m, o); break; }
      DevRec dr; dr.idx = n; dr.pci = "mock";
      probe_device(m, o, "MOCK card " + std::to_string(n), dr);
      devs.push_back(dr);
    }
    if (!devs.empty()) print_topology(devs);
    return rc;
  }

  int found = 0;
  for (int n = 0; n < 16; ++n) {
    if (o.device >= 0 && n != o.device) continue;
    std::string path = "/dev/tenstorrent/" + std::to_string(n);
    if (access(path.c_str(), F_OK) != 0) continue;
    HwReader hw;
    std::string err;
    tt_get_device_info_out info;
    memset(&info, 0, sizeof info);
    if (!hw.open_dev(path, err, &info)) {
      fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
      rc = 1;
      continue;
    }
    ++found;
    char label[160];
    snprintf(label, sizeof label, "%s  pci %04x:%02x:%02x.%u", path.c_str(), info.pci_domain,
             (info.bus_dev_fn >> 8) & 0xff, (info.bus_dev_fn >> 3) & 0x1f, info.bus_dev_fn & 7u);
    if (!o.savePrefix.empty()) { int r2 = save_tile(hw, o, n); if (r2) rc = r2; continue; }
    if (o.dump) { int r2 = dump_words(hw, o); if (r2) rc = r2; continue; }
    DevRec dr; dr.idx = n; dr.pci = label;
    int r2 = probe_device(hw, o, label, dr);
    if (r2) rc = r2;
    devs.push_back(dr);
  }
  if (!found) { fprintf(stderr, "no usable /dev/tenstorrent/N found\n"); return 1; }
  if (!devs.empty()) print_topology(devs);
  return rc;
}
