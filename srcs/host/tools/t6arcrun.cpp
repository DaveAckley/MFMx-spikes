// t6arcrun.cpp -- run the t6arc image on ONE Tensix tile of ONE card and print
// what that tile reads from the ARC tile over the NoC (AICLK and friends).
// New tool; the T6-side twin of arcpeek.  Needs t6pp/t6arc.bin
// (sh t6pp/build.sh t6arc).  The ARC is only READ.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror t6arcrun.cpp -o t6arcrun
//   ./t6arcrun --dev 0                 DRY RUN: reads the tile, writes nothing
//   ./t6arcrun --dev 0 --go            really run (default tile 1:2, NIU0)
//   ./t6arcrun --dev 0 --go --noc 1    the image drives NIU1 instead
//   ./t6arcrun --dev 0 --go --tile 3:4 --passes 5
//   ./t6arcrun --mock --go             fake chip, no hardware (FAKE numbers)
//
// WHAT IT TOUCHES on the chosen tile (and nothing else, on no other tile):
//   soft-reset register 0xFFB121B0: cores held in reset (all five), then
//   ONLY BRISC (bit 11) released, then at exit all held again and the
//   original value restored; L1 0..image size (the image), the control
//   block at 0x8000..0x8600.  tt-kmd is asked to restore the soft-reset
//   register if this process dies (NOC_CLEANUP ioctl).
// IDLE CARD ONLY: no mfmxctl, no other tool.  The tile must have all five
// cores in reset when we start, else it is refused.  Never kill -9 it.
// Exit codes: 0 ok, 1 failure, 2 usage, 4 tile harvested.
#pragma GCC diagnostic ignored "-Wunused-function"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// ---- tt-kmd ioctl ABI (as in arcpeek.cpp / ethfwd22.cpp) ----
constexpr unsigned TT_IOCTL_GET_DEVICE_INFO = 0xFA00;
constexpr unsigned TT_IOCTL_ALLOCATE_TLB    = 0xFA0B;
constexpr unsigned TT_IOCTL_FREE_TLB        = 0xFA0C;
constexpr unsigned TT_IOCTL_CONFIGURE_TLB   = 0xFA0D;
constexpr unsigned TT_IOCTL_SET_NOC_CLEANUP = 0xFA0E;

struct tt_get_device_info_in  { uint32_t output_size_bytes; };
struct tt_get_device_info_out {
  uint32_t output_size_bytes;
  uint16_t vendor_id, device_id, subsystem_vendor_id, subsystem_id;
  uint16_t bus_dev_fn;
  uint16_t max_dma_buf_size_log2;
  uint16_t pci_domain;
};
struct tt_get_device_info {
  tt_get_device_info_in in;
  tt_get_device_info_out out;
};
struct tt_allocate_tlb_in  { uint64_t size; uint64_t reserved; };
struct tt_allocate_tlb_out {
  uint32_t id;
  uint32_t reserved0;
  uint64_t mmap_offset_uc;
  uint64_t mmap_offset_wc;
  uint64_t reserved1;
};
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
struct tt_configure_tlb {
  tt_configure_tlb_in in;
  tt_configure_tlb_out out;
};

constexpr uint16_t PCI_VENDOR_TENSTORRENT = 0x1E52;
constexpr uint16_t PCI_DEVICE_BLACKHOLE   = 0xB140;
constexpr uint64_t TLB_SIZE = 1ull << 21;
constexpr uint64_t TLB_MASK = TLB_SIZE - 1;

// ---- tile registers (as ethfwd22.cpp) ----
constexpr uint32_t NIU0_ADDR = 0xFFB20000;
constexpr uint32_t NOC_ENDPOINT_ID_OFF = 0x048;
constexpr uint32_t NIU_CFG_0_OFF = 0x100;
constexpr uint32_t NIU_CFG_0_HARVESTED = 1u << 12;
constexpr uint32_t SOFT_RESET = 0xFFB121B0;
constexpr uint32_t ALL_RESET = 0x47800;   // BRISC 0x800, TRISC 0x7000, NC 0x40000
constexpr uint32_t SR_BRISC = 0x800;

// ---- the image's control block (t6pp/t6arc.c) ----
constexpr uint32_t CTLB = 0x8000;
constexpr uint32_t CTL_WIN = 0x600;
constexpr uint32_t C_GO = 0x00, C_STOP = 0x04, C_READY = 0x08, C_DONE = 0x0C;
constexpr uint32_t C_NOC = 0x14, C_OWN = 0x18, C_ARC = 0x1C, C_BUF = 0x20;
constexpr uint32_t C_ID = 0x3C, C_PASS = 0x40, C_CSR = 0x44;
constexpr uint32_t R_STATUS = 0x100, R_READS = 0x104, R_FAILED = 0x108;
constexpr uint32_t R_BOOT = 0x10C, R_QINFO = 0x110, R_TDATA = 0x114;
constexpr uint32_t R_TPTR = 0x118, R_VER = 0x11C, R_COUNT = 0x120;
constexpr uint32_t R_AICLK = 0x124, R_AXICLK = 0x128, R_ARCCLK = 0x12C;
constexpr uint32_t R_HB = 0x130, R_LIMIT = 0x134, R_TEMP = 0x138;
constexpr uint32_t R_POWER = 0x13C, R_VCORE = 0x140, R_AOFF = 0x144;
constexpr uint32_t R_L2CPU = 0x148;
constexpr uint32_t READY_WORD = 0x7E57A11Eu;
constexpr uint32_t IMAGE_ID = 0x76500010u;
constexpr unsigned ARC_X = 8, ARC_Y = 0;

volatile std::sig_atomic_t g_sig = 0;
void on_sig(int) { g_sig = 1; }

struct Port {
  virtual ~Port() {}
  virtual bool rd32(unsigned x, unsigned y, uint32_t a, uint32_t& v) = 0;
  virtual bool wr32(unsigned x, unsigned y, uint32_t a, uint32_t v) = 0;
  virtual bool arm_cleanup(unsigned, unsigned, uint32_t, uint32_t) {
    return false;
  }
};

struct HwPort : Port {
  struct Win {
    unsigned x, y;
    uint64_t base;
    uint32_t id;
    volatile uint8_t* p;
  };
  int fd = -1;
  std::vector<Win> wins;
  std::string lasterr;
  bool open_dev(const std::string& path, std::string& err) {
    fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      err = "open " + path + ": " + strerror(errno);
      return false;
    }
    tt_get_device_info di;
    memset(&di, 0, sizeof di);
    di.in.output_size_bytes = sizeof di.out;
    if (ioctl(fd, TT_IOCTL_GET_DEVICE_INFO, &di) < 0) {
      err = "GET_DEVICE_INFO failed";
      return false;
    }
    if (di.out.vendor_id != PCI_VENDOR_TENSTORRENT ||
        di.out.device_id != PCI_DEVICE_BLACKHOLE) {
      err = "not a Blackhole";
      return false;
    }
    return true;
  }
  ~HwPort() override {
    for (Win& w : wins) {
      munmap((void*)w.p, TLB_SIZE);
      tt_free_tlb ft;
      memset(&ft, 0, sizeof ft);
      ft.in.id = w.id;
      (void)ioctl(fd, TT_IOCTL_FREE_TLB, &ft);
    }
    if (fd >= 0) ::close(fd);
  }
  volatile uint32_t* map(unsigned x, unsigned y, uint32_t a) {
    if (a & 3u) {
      lasterr = "unaligned address";
      return nullptr;
    }
    uint64_t base = (uint64_t)a & ~TLB_MASK;
    uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    for (Win& w : wins)
      if (w.x == x && w.y == y && w.base == base)
        return (volatile uint32_t*)(w.p + off);
    if (wins.size() >= 4) {
      lasterr = "too many windows";
      return nullptr;
    }
    tt_allocate_tlb at;
    memset(&at, 0, sizeof at);
    at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) {
      lasterr = std::string("ALLOCATE_TLB: ") + strerror(errno);
      return nullptr;
    }
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) {
      lasterr = std::string("mmap: ") + strerror(errno);
      return nullptr;
    }
    tt_configure_tlb c;
    memset(&c, 0, sizeof c);
    c.in.id = at.out.id;
    c.in.config.addr = base;
    c.in.config.x_start = (uint16_t)x;
    c.in.config.x_end = (uint16_t)x;
    c.in.config.y_start = (uint16_t)y;
    c.in.config.y_end = (uint16_t)y;
    c.in.config.noc = 0;
    c.in.config.mcast = 0;
    c.in.config.ordering = 1;   // strict
    if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) {
      lasterr = std::string("CONFIGURE_TLB: ") + strerror(errno);
      return nullptr;
    }
    wins.push_back({x, y, base, at.out.id, (volatile uint8_t*)p});
    return (volatile uint32_t*)((volatile uint8_t*)p + off);
  }
  bool rd32(unsigned x, unsigned y, uint32_t a, uint32_t& v) override {
    volatile uint32_t* p = map(x, y, a);
    if (!p) {
      fprintf(stderr, "  read (%u,%u) 0x%08x failed: %s\n", x, y, a,
              lasterr.c_str());
      return false;
    }
    v = *p;
    return true;
  }
  bool wr32(unsigned x, unsigned y, uint32_t a, uint32_t v) override {
    volatile uint32_t* p = map(x, y, a);
    if (!p) {
      fprintf(stderr, "  write (%u,%u) 0x%08x failed: %s\n", x, y, a,
              lasterr.c_str());
      return false;
    }
    *p = v;
    return true;
  }
  // tt-kmd writes (addr,data) to tile (x,y) when this fd closes, even if
  // we crash: puts the cores back in reset.
  bool arm_cleanup(unsigned x, unsigned y, uint32_t addr,
                   uint32_t data) override {
    struct {
      uint32_t argsz, flags;
      uint8_t enabled, x, y, noc;
      uint32_t reserved0;
      uint64_t addr, data;
    } req;
    memset(&req, 0, sizeof req);
    req.argsz = sizeof req;
    req.enabled = 1;
    req.x = (uint8_t)x;
    req.y = (uint8_t)y;
    req.noc = 0;
    req.addr = addr;
    req.data = data;
    return ioctl(fd, TT_IOCTL_SET_NOC_CLEANUP, &req) == 0;
  }
};

// ---- MOCK: a fake tile that behaves like the t6arc image (FAKE numbers) ----
struct MockPort : Port {
  std::map<uint32_t, uint32_t> m;
  uint32_t hb = 1000;
  bool running = false;
  MockPort() {
    m[SOFT_RESET] = ALL_RESET;
    m[NIU0_ADDR + NOC_ENDPOINT_ID_OFF] = 0x44;
    m[NIU0_ADDR + NIU_CFG_0_OFF] = 0;
  }
  uint32_t get(uint32_t a) {
    auto it = m.find(a);
    return it == m.end() ? 0u : it->second;
  }
  bool rd32(unsigned, unsigned, uint32_t a, uint32_t& v) override {
    v = get(a);
    return true;
  }
  bool wr32(unsigned, unsigned, uint32_t a, uint32_t v) override {
    m[a] = v;
    if (a == SOFT_RESET) {
      bool rel = !(v & SR_BRISC);
      if (rel && !running) {
        running = true;
        m[CTLB + C_READY] = READY_WORD;
        m[CTLB + C_ID] = IMAGE_ID;
      }
      if (!rel) running = false;
    }
    if (running && a == CTLB + C_GO && v == 1) {
      hb += 3;
      m[CTLB + R_STATUS] = 0;
      m[CTLB + R_READS] = 18;
      m[CTLB + R_FAILED] = 0;
      m[CTLB + R_BOOT] = 5;
      m[CTLB + R_QINFO] = 0x10017a08u;
      m[CTLB + R_TDATA] = 0x1001a1a0u;
      m[CTLB + R_TPTR] = 0x1001a094u;
      m[CTLB + R_VER] = 0x100;
      m[CTLB + R_COUNT] = 65;
      m[CTLB + R_AICLK] = 800;
      m[CTLB + R_AXICLK] = 960;
      m[CTLB + R_ARCCLK] = 800;
      m[CTLB + R_HB] = hb;
      m[CTLB + R_LIMIT] = 1350;
      m[CTLB + R_TEMP] = 0x23899a;
      m[CTLB + R_POWER] = 37;
      m[CTLB + R_VCORE] = 740;
      m[CTLB + R_AOFF] = 14;
      m[CTLB + R_L2CPU] = 800;
      m[CTLB + C_PASS] = get(CTLB + C_PASS) + 1;
      m[CTLB + C_DONE] = 1;
    }
    return true;
  }
};

// ---- the run ----
bool g_go = false;
std::vector<uint32_t> g_img;
unsigned g_bad = 0;

bool write_ok(uint32_t a) {
  if (a & 3u) return false;
  if (a < (uint32_t)g_img.size() * 4u) return true;      // the image at L1 0
  return a >= CTLB && a < CTLB + CTL_WIN;                 // control block
}
uint32_t R(Port& p, unsigned x, unsigned y, uint32_t a) {
  uint32_t v = 0;
  if (!p.rd32(x, y, a, v)) {
    ++g_bad;
    return 0xDEADDEADu;
  }
  return v;
}
void W(Port& p, unsigned x, unsigned y, uint32_t a, uint32_t v) {
  if (!write_ok(a)) {
    fprintf(stderr, "INTERNAL: write 0x%x refused\n", a);
    exit(3);
  }
  if (!g_go) return;
  if (!p.wr32(x, y, a, v)) ++g_bad;
}
// Soft-reset writes allowlist: original, original|ALL, or (original|ALL)
// with only BRISC released.
bool SR(Port& p, unsigned x, unsigned y, uint32_t orig, uint32_t v) {
  if (v != orig && v != (orig | ALL_RESET) &&
      v != ((orig | ALL_RESET) & ~SR_BRISC)) {
    fprintf(stderr, "INTERNAL: soft-reset write 0x%x refused\n", v);
    exit(3);
  }
  if (!g_go) return true;
  uint32_t b = 0;
  if (!p.wr32(x, y, SOFT_RESET, v) || !p.rd32(x, y, SOFT_RESET, b) ||
      b != v) {
    printf("  soft-reset write/readback failed (wrote 0x%x read 0x%x)\n", v,
           b);
    ++g_bad;
    return false;
  }
  return true;
}

void sleep_ms(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool load_image(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    fprintf(stderr, "cannot open image %s (sh t6pp/build.sh t6arc)\n",
            path.c_str());
    return false;
  }
  std::vector<char> b((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  if (b.empty() || b.size() > 0x4000) {
    fprintf(stderr, "image size %zu not in 1..16384\n", b.size());
    return false;
  }
  while (b.size() % 4) b.push_back(0);
  g_img.resize(b.size() / 4);
  memcpy(g_img.data(), b.data(), b.size());
  return true;
}

struct Pass {
  uint32_t status, reads, failed, boot, tdata, tptr, ver, count, aiclk,
      axiclk, arcclk, hb, limit, temp, power, vcore, aoff, l2cpu;
};
Pass read_pass(Port& p, unsigned x, unsigned y) {
  Pass s;
  s.status = R(p, x, y, CTLB + R_STATUS);
  s.reads = R(p, x, y, CTLB + R_READS);
  s.failed = R(p, x, y, CTLB + R_FAILED);
  s.boot = R(p, x, y, CTLB + R_BOOT);
  s.tdata = R(p, x, y, CTLB + R_TDATA);
  s.tptr = R(p, x, y, CTLB + R_TPTR);
  s.ver = R(p, x, y, CTLB + R_VER);
  s.count = R(p, x, y, CTLB + R_COUNT);
  s.aiclk = R(p, x, y, CTLB + R_AICLK);
  s.axiclk = R(p, x, y, CTLB + R_AXICLK);
  s.arcclk = R(p, x, y, CTLB + R_ARCCLK);
  s.hb = R(p, x, y, CTLB + R_HB);
  s.limit = R(p, x, y, CTLB + R_LIMIT);
  s.temp = R(p, x, y, CTLB + R_TEMP);
  s.power = R(p, x, y, CTLB + R_POWER);
  s.vcore = R(p, x, y, CTLB + R_VCORE);
  s.aoff = R(p, x, y, CTLB + R_AOFF);
  s.l2cpu = R(p, x, y, CTLB + R_L2CPU);
  return s;
}

bool run(Port& p, unsigned x, unsigned y, unsigned noc, unsigned passes,
         int gap_ms) {
  uint32_t ep = 0, cfg = 0, sr = 0;
  p.rd32(x, y, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, ep);
  p.rd32(x, y, NIU0_ADDR + NIU_CFG_0_OFF, cfg);
  p.rd32(x, y, SOFT_RESET, sr);
  printf("  tile (%u,%u): NIU endpoint id 0x%08x, NIU cfg 0x%08x%s, "
         "soft-reset 0x%08x (cores %s)\n",
         x, y, ep, cfg, (cfg & NIU_CFG_0_HARVESTED) ? " HARVESTED" : "",
         sr, (sr & ALL_RESET) == ALL_RESET ? "all in reset" : "NOT all in reset");
  if (cfg & NIU_CFG_0_HARVESTED) {
    printf("  tile is harvested: nothing done (exit 4)\n");
    return false;
  }
  if ((sr & ALL_RESET) != ALL_RESET) {
    printf("  cores of this tile are not all in reset (in use?): refused\n");
    return false;
  }
  if (!g_go) {
    printf("  (dry-run) would write the %zu-byte image at L1 0 and the "
           "control block at 0x%x, release BRISC only, run %u passes on "
           "NIU%u, then restore soft-reset 0x%08x.  Add --go.\n",
           g_img.size() * 4, CTLB, passes, noc, sr);
    return true;
  }
  const uint32_t own = (y << 6) | x;
  const uint32_t arcnode = (ARC_Y << 6) | ARC_X;
  bool touched = true, ok = true;
  bool armed = p.arm_cleanup(x, y, SOFT_RESET, sr);
  printf("  NOC_CLEANUP (kernel restores soft-reset if we die): %s\n",
         armed ? "armed" : "NOT available");
  SR(p, x, y, sr, sr | ALL_RESET);
  for (size_t i = 0; i < g_img.size(); ++i) W(p, x, y, 4u * i, g_img[i]);
  unsigned bad = 0;
  for (size_t i = 0; i < g_img.size(); ++i)
    bad += (R(p, x, y, 4u * (uint32_t)i) != g_img[i]);
  if (bad) {
    printf("  image readback mismatch in %u words; aborting\n", bad);
    ok = false;
    goto restore;
  }
  for (uint32_t a = CTLB; a < CTLB + CTL_WIN; a += 4) W(p, x, y, a, 0);
  W(p, x, y, CTLB + C_NOC, noc);
  W(p, x, y, CTLB + C_OWN, own);
  W(p, x, y, CTLB + C_ARC, arcnode);
  W(p, x, y, CTLB + C_BUF, 3);
  SR(p, x, y, sr, (sr | ALL_RESET) & ~SR_BRISC);
  {
    bool up = false;
    for (int i = 0; i < 1000 && !g_sig; ++i) {
      if (R(p, x, y, CTLB + C_READY) == READY_WORD) {
        up = true;
        break;
      }
      sleep_ms(1);
    }
    if (!up) {
      printf("  image did not start (no ready word): FAIL\n");
      ok = false;
      goto restore;
    }
    printf("  image up: id 0x%08x, csr 0x7c0 readback 0x%08x\n",
           R(p, x, y, CTLB + C_ID), R(p, x, y, CTLB + C_CSR));
  }
  {
    uint32_t hb0 = 0;
    unsigned good = 0;
    for (unsigned n = 1; n <= passes && !g_sig; ++n) {
      W(p, x, y, CTLB + C_GO, 1);
      bool got = false;
      for (int i = 0; i < 2000 && !g_sig; ++i) {
        if (R(p, x, y, CTLB + C_PASS) >= n) {
          got = true;
          break;
        }
        sleep_ms(1);
      }
      if (!got) {
        printf("  pass %u: image did not finish in 2 s: FAIL\n", n);
        ok = false;
        break;
      }
      Pass s = read_pass(p, x, y);
      printf("  pass %u: status %u (%s)  NoC reads %u, failed %u\n", n,
             s.status,
             s.status == 0   ? "ok"
             : s.status == 1 ? "bad pointer"
             : s.status == 2 ? "read timeout"
             : s.status == 3 ? "bad entry count"
             : s.status == 4 ? "AICLK tag missing"
                             : "?",
             s.reads, s.failed);
      if (s.status == 0) {
        printf("    boot 0x%x  table 0x%08x data 0x%08x version 0x%x "
               "entries %u\n",
               s.boot, s.tptr, s.tdata, s.ver, s.count);
        printf("    AICLK  raw 0x%x = %u MHz (low16), high16 %u  "
               "[AICLK tag offset %u]\n",
               s.aiclk, s.aiclk & 0xffff, s.aiclk >> 16, s.aoff);
        printf("    AXICLK %u  ARCCLK %u  L2CPUCLK0 %d  AICLK_LIMIT_MAX %u\n",
               s.axiclk, s.arcclk,
               s.l2cpu == 0xFFFFFFFFu ? -1 : (int)s.l2cpu, s.limit);
        printf("    heartbeat %u%s  POWER %u  VCORE %u  ASIC_TEMP %.2f C\n",
               s.hb, n > 1 ? (s.hb != hb0 ? " (moving)" : " (NOT moving)") : "",
               s.power, s.vcore,
               (int32_t)s.temp / 65536.0);
        if ((s.aiclk & 0xffff) != 0 && (n == 1 || s.hb != hb0)) ++good;
        hb0 = s.hb;
      } else {
        ok = false;
      }
      W(p, x, y, CTLB + C_GO, 0);
      if (n < passes) sleep_ms(gap_ms);
    }
    if (g_sig) {
      printf("  interrupted\n");
      ok = false;
    }
    printf("  %s: %u of %u passes read AICLK from the T6 (NIU%u)\n",
           ok && good == passes ? "T6 READ OK" : "T6 READ NOT CLEAN", good,
           passes, noc);
    if (good != passes) ok = false;
  }
restore:
  W(p, x, y, CTLB + C_STOP, 1);
  W(p, x, y, CTLB + C_GO, 0);
  sleep_ms(5);
  if (touched) {
    (void)SR(p, x, y, sr, sr | ALL_RESET);
    if (SR(p, x, y, sr, sr))
      printf("  tile (%u,%u) soft-reset restored to 0x%08x\n", x, y, sr);
  }
  return ok && g_bad == 0;
}

void usage() {
  fprintf(stderr,
          "usage: t6arcrun --dev N [--tile X:Y] [--noc 0|1] [--passes N]\n"
          "                [--image FILE] [--go] | --mock [--go]\n"
          "  default is a dry run; --go runs t6arc.bin on one Tensix tile\n"
          "  (default 1:2, NoC0 coordinates).  Idle card only.\n");
}

}  // namespace

int main(int argc, char** argv) {
  int dev = -1;
  unsigned x = 1, y = 2, noc = 0, passes = 3;
  bool mock = false;
  std::string image = "t6pp/t6arc.bin";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    unsigned v = 0, q = 0;
    if (a == "--dev" && i + 1 < argc) dev = atoi(argv[++i]);
    else if (a == "--tile" && i + 1 < argc &&
             sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) {
      x = v;
      y = q;
      ++i;
    } else if (a == "--noc" && i + 1 < argc) noc = (unsigned)atoi(argv[++i]);
    else if (a == "--passes" && i + 1 < argc)
      passes = (unsigned)atoi(argv[++i]);
    else if (a == "--image" && i + 1 < argc) image = argv[++i];
    else if (a == "--go") g_go = true;
    else if (a == "--mock") mock = true;
    else {
      usage();
      return 2;
    }
  }
  if (noc > 1 || passes < 1 || passes > 50 ||
      (!(x >= 1 && x <= 7) && !(x >= 10 && x <= 16)) || y < 2 || y > 11) {
    fprintf(stderr, "bad option value (Tensix columns are x 1..7 and "
                    "10..16, rows 2..11)\n");
    return 2;
  }
  if (!mock && dev < 0) {
    usage();
    return 2;
  }
  if (!load_image(image)) return 2;
  signal(SIGINT, on_sig);
  signal(SIGTERM, on_sig);
  signal(SIGHUP, on_sig);
  if (mock) {
    MockPort mp;
    printf("MOCK: all numbers below are FAKE\n== mock card\n");
    return run(mp, x, y, noc, passes, 1) ? 0 : 1;
  }
  HwPort hp;
  std::string err;
  if (!hp.open_dev("/dev/tenstorrent/" + std::to_string(dev), err)) {
    fprintf(stderr, "dev%d: %s\n", dev, err.c_str());
    return 1;
  }
  printf("== card %d (%s)\n", dev, g_go ? "GO" : "dry run");
  return run(hp, x, y, noc, passes, 300) ? 0 : 1;
}
