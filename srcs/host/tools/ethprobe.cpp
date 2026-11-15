// ethprobe.cpp -- READ-ONLY probe of Blackhole Ethernet tiles, tt-kmd only.
//
// Purpose: step 1 of the 'eth-seam' project.  Find out what the base
// firmware on each ETH tile is doing, so we can decide how to move MFMx
// packets card-to-card over the QSFP-DD cables without PCIe.  Specifically:
//   * which ETH tiles exist / are harvested, and what their link state is
//   * is the ERISC base firmware alive (heartbeat ticking)?
//   * does the 'baseline data movement service' queue block exist, and
//     where (pointer published at L1 0x170 on Wormhole; UNVERIFIED on BH)
//   * peer/chip-info snippets the firmware leaves in its boot-results block
//
// SAFETY: this program NEVER writes to the device.  It allocates one 2 MiB
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
//   g++ -std=c++17 -O1 -Wall -Wextra -Werror ethprobe.cpp -o ethprobe
//
// Usage:
//   ./ethprobe                     # probe every /dev/tenstorrent/N
//   ./ethprobe --device 2          # just one card
//   ./ethprobe --raw               # also hexdump boot params/results words
//   ./ethprobe --tile 4 --dump 0x7cc00 16   # read 16 words of ETH tile X=4
//   ./ethprobe --mock              # fake data (NOT hardware!) to test the tool
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

// Fake device for testing the tool's logic/format.  NOT HARDWARE.  Loosely
// modeled on the ethdump README sample table (14 ETH tiles, E4/E6 etc).
struct MockReader : Reader {
  static int eth_index_for_x(unsigned x) {
    static const int map_lo[8] = {-1, 0, 2, 4, 6, 8, 10, 12};   // x=1..7
    static const int map_hi[8] = {13, 11, 9, 7, 5, 3, 1, -1};   // x=10..16
    if (x >= 1 && x <= 7) return map_lo[x];
    if (x >= 10 && x <= 16) return map_hi[x - 10];
    return -1;
  }
  bool rd32(unsigned x, unsigned y, uint32_t addr, uint32_t& out) override {
    out = 0;
    int e = (y == 1) ? eth_index_for_x(x) : -1;
    if (e < 0) { out = 0x00010000u; return true; }  // some non-ETH endpoint id
    bool harvested = (e == 5 || e == 8);
    bool noport = (e <= 3);
    if (addr == NIU0_ADDR + NOC_ENDPOINT_ID_OFFSET) out = 0x00020000u | (uint32_t)e;
    else if (addr == NIU0_ADDR + NIU_CFG_0_OFFSET) out = harvested ? NIU_CFG_0_HARVESTED : 0;
    else if (addr == NIU0_ADDR + NOC_ID_LOGICAL_OFFSET) out = (25u << 6) | (20u + (unsigned)(e / 2) + (e & 1 ? 6u : 0u));
    else if (addr == SOFT_RESET_ADDR) out = 0xFFFFE7FFu;  // E1 released, E0 running (made up)
    else if (addr == ETH_SVC_QUEUE_PTR_ADDR) out = 0x11000;
    else if (addr >= ETH_BOOT_PARAMS_ADDR && addr < ETH_BOOT_PARAMS_ADDR + 0x400) {
      unsigned w = (addr - ETH_BOOT_PARAMS_ADDR) / 4;
      if (w == 0) out = 0x3FFF & ~((1u << 5) | (1u << 8));
      else if (w == 36) out = 0x208C47;
      else if (w == 37) out = 0x052BC0;
    } else if (addr >= ETH_BOOT_RESULTS_ADDR && addr < ETH_BOOT_RESULTS_ADDR + 0x400) {
      unsigned w = (addr - ETH_BOOT_RESULTS_ADDR) / 4;
      if (w == 1) out = noport ? 3 : ((e % 2) ? 1 : 2);
      else if (w == 2) out = noport ? 1 : ((e % 2) ? 2 : 6);
      else if (w == 28) {
        using namespace std::chrono;
        out = (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
      } else if (w == 32) out = noport ? 0 : 0xC0DE1234;
      else if (w == 33) out = 2 + (uint32_t)((e - 4) / 4);
      else if (w == 34) out = (e & 1) ? 0x0F : 0xF0;
      else if (w == 247) out = 0;
    } else if (addr >= 0x11000 && addr < 0x11400) {
      out = 0;  // queue block exists but is idle
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

struct EthTile {
  unsigned x, y;
  unsigned ethIdx;
  bool harvested;
  uint32_t portStatus;
  uint32_t hb1;
  uint32_t qbase;
};

struct Opts {
  int device = -1;       // -1 = all
  bool raw = false;
  bool mock = false;
  int tileX = -1;
  bool dump = false;
  uint32_t dumpAddr = 0;
  unsigned dumpWords = 0;
  unsigned hbMs = 100;
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

int probe_device(Reader& r, const Opts& o, const std::string& label) {
  printf("=== %s ===\n", label.c_str());
  printf("|Tile|NoC0 XY |Logical  |Port   |Training    |Serdes          |MAC (derived)    |HB(hex)  |SvcQ@0x170|\n");
  printf("|----|--------|---------|-------|------------|----------------|-----------------|---------|----------|\n");

  std::vector<EthTile> tiles;
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
    EthTile t{};
    t.x = x; t.y = y; t.ethIdx = ep & 0xff;
    (void)r.rd32(x, y, NIU0_ADDR + NIU_CFG_0_OFFSET, cfg);
    printf("|E%-3u|X=%-2u,Y=%u|", t.ethIdx, x, y);
    t.harvested = (cfg & NIU_CFG_0_HARVESTED) != 0;
    if (t.harvested) {
      printf("Harvested|N/A    |N/A         |N/A             |N/A              |N/A      |N/A       |\n");
      tiles.push_back(t);
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
      tiles.push_back(t);
      continue;
    }
    t.portStatus = res[1];
    printf("%-7s|", port_status_str(res[1]));
    uint32_t train = (res[1] == 0) ? 0 : res[2];
    if (train < 11) printf("%-12s|", train_status_str(train)); else printf("Status %-5u|", train);
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
    if (res[1] == 3) {
      printf("N/A              |");
    } else {
      uint32_t major, minor;
      if (res[247] == 1) { major = res[243]; minor = res[244]; }
      else {
        uint32_t lid = (uint32_t)__builtin_popcount(par[0] & ((1u << (ep & 0x1f)) - 1));
        major = par[36]; minor = par[37] + lid;
      }
      printf("%02x:%02x:%02x:%02x:%02x:%02x|", (uint8_t)(major >> 16), (uint8_t)(major >> 8), (uint8_t)major,
             (uint8_t)(minor >> 16), (uint8_t)(minor >> 8), (uint8_t)minor);
    }
    t.hb1 = res[28];
    printf("%08x |", t.hb1);
    uint32_t qp = 0;
    (void)r.rd32(x, y, ETH_SVC_QUEUE_PTR_ADDR, qp);
    t.qbase = qp;
    printf("0x%08x|\n", qp);
    tiles.push_back(t);

    if (o.raw) {
      print_hexdump(r, x, y, ETH_BOOT_PARAMS_ADDR, 48, "boot_params");
      print_hexdump(r, x, y, ETH_BOOT_RESULTS_ADDR, 64, "boot_results[0..63]");
      print_hexdump(r, x, y, ETH_BOOT_RESULTS_ADDR + 4 * 232, 24, "boot_results[232..255] (peer chip-info exchange?)");
    }
  }

  // Heartbeat: second sample.
  std::this_thread::sleep_for(std::chrono::milliseconds(o.hbMs));
  printf("\nHeartbeat (u32 @0x%08x), second sample after %u ms:\n", ETH_HEARTBEAT_ADDR, o.hbMs);
  for (const EthTile& t : tiles) {
    if (t.harvested || t.portStatus == 3) continue;
    uint32_t hb2 = 0;
    if (!r.rd32(t.x, t.y, ETH_HEARTBEAT_ADDR, hb2)) { printf("  E%-2u: read failed\n", t.ethIdx); continue; }
    printf("  E%-2u (X=%-2u): %08x -> %08x  %s\n", t.ethIdx, t.x, t.hb1, hb2,
           hb2 != t.hb1 ? "changing" : "UNCHANGED (stalled, or not a ticking counter)");
  }

  // Base-FW service queue peek (Wormhole-documented layout; UNVERIFIED on BH).
  printf("\nBase-FW service queue block (WORMHOLE layout assumed; treat as a hypothesis):\n");
  for (const EthTile& t : tiles) {
    if (t.harvested || t.portStatus == 3) continue;
    bool plausible = t.qbase != 0 && (t.qbase % 16u) == 0 && t.qbase + 0x400u <= ETH_L1_SIZE;
    printf("  E%-2u (X=%-2u): ptr@0x170 = 0x%08x %s\n", t.ethIdx, t.x, t.qbase,
           plausible ? "(plausible)" : "(NOT a plausible queue address; service layout likely differs on BH)");
    if (plausible) {
      print_queue(r, t.x, t.y, t.qbase + QB_SQ_OFFSET, "sq");
      print_queue(r, t.x, t.y, t.qbase + QB_CQ_OFFSET, "cq");
    }
  }

  // Soft-reset register: which of E0/E1 are held in reset?
  printf("\nSoft-reset register 0xFFB121B0 (bit 11 = E0, bit 12 = E1; 1 = held in reset):\n");
  for (const EthTile& t : tiles) {
    if (t.harvested) continue;
    uint32_t sr = 0;
    if (!r.rd32(t.x, t.y, SOFT_RESET_ADDR, sr)) { printf("  E%-2u: read failed\n", t.ethIdx); continue; }
    printf("  E%-2u (X=%-2u): 0x%08x  E0=%s E1=%s\n", t.ethIdx, t.x, sr, (sr & 0x0800) ? "reset" : "running",
           (sr & 0x1000) ? "reset" : "running");
  }

  printf("\n");
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
          "ethprobe -- READ-ONLY Blackhole Ethernet tile probe (tt-kmd only)\n"
          "  --device N        probe only /dev/tenstorrent/N (default: all found)\n"
          "  --tile X          restrict to ETH tile at NoC0 X (Y=1)\n"
          "  --dump ADDR N     with --tile: hexdump N words at ADDR (ETH L1 or regs)\n"
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
    else if (a == "--dump" && i + 2 < argc && parse_u32(argv[i + 1], o.dumpAddr) && parse_u32(argv[i + 2], v)) {
      o.dump = true; o.dumpWords = v; i += 2;
    }
    else if (a == "--raw") o.raw = true;
    else if (a == "--mock") o.mock = true;
    else { usage(); return 2; }
  }

  if (o.mock) {
    printf("*** MOCK MODE: all values below are FAKE, generated by the tool itself ***\n");
    MockReader m;
    return o.dump ? dump_words(m, o) : probe_device(m, o, "MOCK device");
  }

  int rc = 0, found = 0;
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
    int r2 = o.dump ? dump_words(hw, o) : probe_device(hw, o, label);
    if (r2) rc = r2;
  }
  if (!found) { fprintf(stderr, "no usable /dev/tenstorrent/N found\n"); return 1; }
  return rc;
}
