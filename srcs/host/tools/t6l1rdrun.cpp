// t6l1rdrun.cpp -- run the t6l1rd image on ONE Tensix tile (A) of ONE card; it
// reads ANOTHER Tensix tile's (B's) L1 over the NoC at many source / buffer
// alignments, and this tool prints where the NIU put the data.  New tool.
// Needs t6pp/t6l1rd.bin (sh t6pp/build.sh t6l1rd).  Question answered: for
// a read from a T6's L1, does the data land at the return address (so the
// modulo-16 matching of NRI3::blockingL1ReadNoC0 is enough) or at (return &
// ~63) + (source & 63) as for the ARC tile (ERISC_NOTES 9.19)?
//
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror t6l1rdrun.cpp -o t6l1rdrun
//   ./t6l1rdrun --dev 0                 DRY RUN: reads the tiles, writes nothing
//   ./t6l1rdrun --dev 0 --go            tile A 1:2 reads tile B 2:2 (NoC0 coords)
//   ./t6l1rdrun --dev 0 --go --tile 1:2 --peer 3:4 --noc 1
//   ./t6l1rdrun --mock --go             fake chip (FAKE numbers)
//
// WHAT IT TOUCHES: on tile A the soft-reset register 0xFFB121B0 (cores held,
// then ONLY BRISC released, then held and the original restored), L1 0..image
// size and the control block 0x8000..0x8900; on tile B only L1 0x20000..0x20100
// (the test pattern; B's cores and registers are not touched).  tt-kmd
// restores A's soft-reset if this process dies.  IDLE CARD ONLY, tile A needs
// all five cores in reset, never kill -9.  Exit: 0 ok, 1 failure, 2 usage,
// 4 tile harvested.
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

// ---- the image's control block (t6pp/t6l1rd.c) ----
constexpr uint32_t CTLB = 0x8000;
constexpr uint32_t CTL_WIN = 0x900;
constexpr uint32_t C_GO = 0x00, C_STOP = 0x04, C_READY = 0x08, C_DONE = 0x0C;
constexpr uint32_t C_NOC = 0x14, C_OWN = 0x18, C_PEER = 0x1C, C_BUF = 0x20;
constexpr uint32_t C_ID = 0x3C, C_PASS = 0x40, C_CSR = 0x44;
constexpr uint32_t R_A = 0x100, R_B = 0x400, R_READS = 0x4C0, R_FAILED = 0x4C4;
constexpr uint32_t RB = CTLB + 0x800;       // the image's buffer region
constexpr uint32_t PEERB = 0x20000;         // pattern in tile B's L1
constexpr uint32_t PEER_WORDS = 64;
constexpr uint32_t READY_WORD = 0x7E57A11Eu;
constexpr uint32_t IMAGE_ID = 0x76500020u;
const unsigned NS[3] = {1, 4, 12};
inline uint32_t val(uint32_t a) { return (a * 0x9E3779B1u) ^ 0x5A5A5A5Au; }


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


// ---- MOCK: a fake pair of tiles; the fake image says every read was right ----
struct MockPort : Port {
  std::map<uint32_t, uint32_t> m;
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
      bool rel = !(v & 0x800);
      if (rel && !running) {
        running = true;
        m[CTLB + C_READY] = READY_WORD;
        m[CTLB + C_ID] = IMAGE_ID;
      }
      if (!rel) running = false;
    }
    if (running && a == CTLB + C_GO && v == 1) {
      const char* rule = getenv("T6L1RD_MOCK_RULE64");   // test hook: mock the ARC rule
      for (unsigned i = 0; i < 192; ++i) {
        uint32_t v = 1;
        if (rule && rule[0] == '1') {
          unsigned k = i / 48, s2 = (i / 3) % 16;
          uint32_t src = PEERB + 4 * s2, off = 16 * k + (src & 15);
          if ((off & 63) != (src & 63))
            v = ((((off & ~63u) / 4) + (src & 63) / 4 + 1) << 8);
        }
        m[CTLB + R_A + 4 * i] = v;
      }
      for (unsigned i = 0; i < 48; ++i) m[CTLB + R_B + 4 * i] = 1;
      m[CTLB + R_READS] = 240;
      m[CTLB + R_FAILED] = 0;
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

bool write_ok_a(uint32_t a) {
  if (a & 3u) return false;
  if (a < (uint32_t)g_img.size() * 4u) return true;      // the image at L1 0
  return a >= CTLB && a < CTLB + CTL_WIN;                 // control block
}
bool write_ok_b(uint32_t a) {
  return !(a & 3u) && a >= PEERB && a < PEERB + 4 * PEER_WORDS;
}
uint32_t R(Port& p, unsigned x, unsigned y, uint32_t a) {
  uint32_t v = 0;
  if (!p.rd32(x, y, a, v)) {
    ++g_bad;
    return 0xDEADDEADu;
  }
  return v;
}
void WA(Port& p, unsigned x, unsigned y, uint32_t a, uint32_t v) {
  if (!write_ok_a(a)) {
    fprintf(stderr, "INTERNAL: tile A write 0x%x refused\n", a);
    exit(3);
  }
  if (!g_go) return;
  if (!p.wr32(x, y, a, v)) ++g_bad;
}
void WB(Port& p, unsigned x, unsigned y, uint32_t a, uint32_t v) {
  if (!write_ok_b(a)) {
    fprintf(stderr, "INTERNAL: tile B write 0x%x refused\n", a);
    exit(3);
  }
  if (!g_go) return;
  if (!p.wr32(x, y, a, v)) ++g_bad;
}
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
    fprintf(stderr, "cannot open image %s (sh t6pp/build.sh t6l1rd)\n",
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

// Print one verdict cell: "ok", "+N"/"-N" bytes of shift, or "??".
std::string cell(uint32_t v, uint32_t bufoff) {
  if ((v & 0xff) == 1) return "ok";
  if ((v & 0xff) == 0xff) return "TO";
  uint32_t found = v >> 8;
  if (!found) return "??";
  int shift = (int)(found - 1) * 4 - (int)bufoff;
  char b[16];
  snprintf(b, sizeof b, "%+d", shift);
  return b;
}

bool analyse(const std::vector<uint32_t>& A, const std::vector<uint32_t>& B) {
  unsigned okA = 0, okB = 0, agree = 0;
  for (unsigned k = 0; k < 4; ++k)
    for (unsigned s = 0; s < 16; ++s)
      for (unsigned n = 0; n < 3; ++n) {
        uint32_t v = A[(k * 16 + s) * 3 + n];
        bool ok = (v & 0xff) == 1;
        uint32_t src = PEERB + 4 * s, buf = 16 * k + (src & 15);
        bool pred = (buf & 63) == (src & 63);
        okA += ok;
        agree += (ok == pred);
      }
  for (unsigned i = 0; i < 48; ++i) okB += (B[i] & 0xff) == 1;
  printf("  method A (buffer = RB + 16k + src%%16, the NRI3 rule, 4 buffer "
         "alignments k): %u of 192 reads right\n", okA);
  printf("  method B (buffer = RB + src%%64, the ARC rule): %u of 48 reads "
         "right\n", okB);
  printf("  shift tables (bytes the data landed from where expected; ok = "
         "right):\n");
  printf("   src%%64 |");
  for (unsigned k = 0; k < 4; ++k) printf("  k=%u n=1/4/12   |", k);
  printf("  B n=1/4/12\n");
  for (unsigned s = 0; s < 16; ++s) {
    printf("   %5u |", 4 * s);
    for (unsigned k = 0; k < 4; ++k) {
      uint32_t src = PEERB + 4 * s, off = 16 * k + (src & 15);
      printf(" %-4s%-4s%-4s |", cell(A[(k * 16 + s) * 3 + 0], off).c_str(),
             cell(A[(k * 16 + s) * 3 + 1], off).c_str(),
             cell(A[(k * 16 + s) * 3 + 2], off).c_str());
    }
    uint32_t off = (PEERB + 4 * s) & 63;
    printf(" %-4s%-4s%-4s\n", cell(B[s * 3 + 0], off).c_str(),
           cell(B[s * 3 + 1], off).c_str(), cell(B[s * 3 + 2], off).c_str());
  }
  printf("  hub image case: buffer alignment k=1 (privateL1ReadBuffer is 16 "
         "mod 64) reading source 48 mod 64 (0xb0): %s %s %s\n",
         cell(A[(1 * 16 + 12) * 3 + 0], 16).c_str(),
         cell(A[(1 * 16 + 12) * 3 + 1], 16).c_str(),
         cell(A[(1 * 16 + 12) * 3 + 2], 16).c_str());
  if (okA == 192 && okB == 48) {
    printf("  RESULT: reads from a T6's L1 land AT THE RETURN ADDRESS: "
           "matching the buffer modulo 16 (NRI3::blockingL1ReadNoC0) is "
           "enough; the mod-64 rule is specific to the ARC tile.\n");
    return true;
  }
  if (agree == 192 && okB == 48) {
    printf("  RESULT: L1 reads follow the MOD-64 rule too: blockingL1ReadNoC0 "
           "is right only when its buffer matches the source modulo 64.\n");
    return true;
  }
  printf("  RESULT: neither simple rule fits (agreement with the mod-64 "
         "prediction %u of 192): read the table.\n", agree);
  return true;
}

bool run(Port& p, unsigned ax, unsigned ay, unsigned bx, unsigned by,
         unsigned noc) {
  uint32_t ep = 0, cfg = 0, sr = 0, bcfg = 0;
  p.rd32(ax, ay, NIU0_ADDR + NOC_ENDPOINT_ID_OFF, ep);
  p.rd32(ax, ay, NIU0_ADDR + NIU_CFG_0_OFF, cfg);
  p.rd32(ax, ay, SOFT_RESET, sr);
  p.rd32(bx, by, NIU0_ADDR + NIU_CFG_0_OFF, bcfg);
  printf("  tile A (%u,%u): NIU endpoint id 0x%08x, NIU cfg 0x%08x%s, "
         "soft-reset 0x%08x (cores %s)\n",
         ax, ay, ep, cfg, (cfg & NIU_CFG_0_HARVESTED) ? " HARVESTED" : "", sr,
         (sr & ALL_RESET) == ALL_RESET ? "all in reset" : "NOT all in reset");
  printf("  tile B (%u,%u): NIU cfg 0x%08x%s\n", bx, by, bcfg,
         (bcfg & NIU_CFG_0_HARVESTED) ? " HARVESTED" : "");
  if ((cfg & NIU_CFG_0_HARVESTED) || (bcfg & NIU_CFG_0_HARVESTED)) {
    printf("  a tile is harvested: nothing done (exit 4)\n");
    return false;
  }
  if ((sr & ALL_RESET) != ALL_RESET) {
    printf("  cores of tile A are not all in reset (in use?): refused\n");
    return false;
  }
  if (!g_go) {
    printf("  (dry-run) would write the %zu-byte image at A's L1 0, the "
           "control block at 0x%x, the %u-word pattern at B's L1 0x%x, "
           "release A's BRISC only, read 240 times over NIU%u, then restore "
           "A's soft-reset 0x%08x.  Add --go.\n",
           g_img.size() * 4, CTLB, PEER_WORDS, PEERB, noc, sr);
    return true;
  }
  const uint32_t own = (ay << 6) | ax, peer = (by << 6) | bx;
  bool ok = true;
  bool armed = p.arm_cleanup(ax, ay, SOFT_RESET, sr);
  printf("  NOC_CLEANUP (kernel restores soft-reset if we die): %s\n",
         armed ? "armed" : "NOT available");
  SR(p, ax, ay, sr, sr | ALL_RESET);
  for (size_t i = 0; i < g_img.size(); ++i) WA(p, ax, ay, 4u * i, g_img[i]);
  unsigned bad = 0;
  for (size_t i = 0; i < g_img.size(); ++i)
    bad += (R(p, ax, ay, 4u * (uint32_t)i) != g_img[i]);
  if (bad) {
    printf("  image readback mismatch in %u words; aborting\n", bad);
    ok = false;
    goto restore;
  }
  for (uint32_t a = CTLB; a < CTLB + CTL_WIN; a += 4) WA(p, ax, ay, a, 0);
  WA(p, ax, ay, CTLB + C_NOC, noc);
  WA(p, ax, ay, CTLB + C_OWN, own);
  WA(p, ax, ay, CTLB + C_PEER, peer);
  WA(p, ax, ay, CTLB + C_BUF, 3);
  for (uint32_t i = 0; i < PEER_WORDS; ++i)
    WB(p, bx, by, PEERB + 4 * i, val(PEERB + 4 * i));
  for (uint32_t i = 0; i < PEER_WORDS; ++i)
    bad += (R(p, bx, by, PEERB + 4 * i) != val(PEERB + 4 * i));
  if (bad) {
    printf("  pattern readback mismatch on tile B; aborting\n");
    ok = false;
    goto restore;
  }
  SR(p, ax, ay, sr, (sr | ALL_RESET) & ~SR_BRISC);
  {
    bool up = false;
    for (int i = 0; i < 1000 && !g_sig; ++i) {
      if (R(p, ax, ay, CTLB + C_READY) == READY_WORD) {
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
           R(p, ax, ay, CTLB + C_ID), R(p, ax, ay, CTLB + C_CSR));
  }
  {
    WA(p, ax, ay, CTLB + C_GO, 1);
    bool got = false;
    for (int i = 0; i < 5000 && !g_sig; ++i) {
      if (R(p, ax, ay, CTLB + C_PASS) >= 1) {
        got = true;
        break;
      }
      sleep_ms(1);
    }
    if (!got) {
      printf("  image did not finish in 5 s: FAIL\n");
      ok = false;
      goto restore;
    }
    printf("  NoC reads done %u, failed %u\n", R(p, ax, ay, CTLB + R_READS),
           R(p, ax, ay, CTLB + R_FAILED));
    std::vector<uint32_t> A(192), B(48);
    for (unsigned i = 0; i < 192; ++i) A[i] = R(p, ax, ay, CTLB + R_A + 4 * i);
    for (unsigned i = 0; i < 48; ++i) B[i] = R(p, ax, ay, CTLB + R_B + 4 * i);
    analyse(A, B);
    WA(p, ax, ay, CTLB + C_GO, 0);
  }
restore:
  WA(p, ax, ay, CTLB + C_STOP, 1);
  WA(p, ax, ay, CTLB + C_GO, 0);
  sleep_ms(5);
  (void)SR(p, ax, ay, sr, sr | ALL_RESET);
  if (SR(p, ax, ay, sr, sr))
    printf("  tile A (%u,%u) soft-reset restored to 0x%08x\n", ax, ay, sr);
  return ok && g_bad == 0;
}

void usage() {
  fprintf(stderr,
          "usage: t6l1rdrun --dev N [--tile X:Y] [--peer X:Y] [--noc 0|1]\n"
          "                 [--image FILE] [--go] | --mock [--go]\n"
          "  default is a dry run; --go runs t6l1rd.bin on tile A (default\n"
          "  1:2) which reads tile B (default 2:2).  Idle card only.\n");
}

bool tensix(unsigned x, unsigned y) {
  return ((x >= 1 && x <= 7) || (x >= 10 && x <= 16)) && y >= 2 && y <= 11;
}

}  // namespace

int main(int argc, char** argv) {
  int dev = -1;
  unsigned ax = 1, ay = 2, bx = 2, by = 2, noc = 0;
  bool mock = false;
  std::string image = "t6pp/t6l1rd.bin";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    unsigned v = 0, q = 0;
    if (a == "--dev" && i + 1 < argc) dev = atoi(argv[++i]);
    else if (a == "--tile" && i + 1 < argc &&
             sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) {
      ax = v;
      ay = q;
      ++i;
    } else if (a == "--peer" && i + 1 < argc &&
               sscanf(argv[i + 1], "%u:%u", &v, &q) == 2) {
      bx = v;
      by = q;
      ++i;
    } else if (a == "--noc" && i + 1 < argc) noc = (unsigned)atoi(argv[++i]);
    else if (a == "--image" && i + 1 < argc) image = argv[++i];
    else if (a == "--go") g_go = true;
    else if (a == "--mock") mock = true;
    else {
      usage();
      return 2;
    }
  }
  if (noc > 1 || !tensix(ax, ay) || !tensix(bx, by) ||
      (ax == bx && ay == by)) {
    fprintf(stderr, "bad option value (two different Tensix tiles: columns "
                    "x 1..7 and 10..16, rows 2..11)\n");
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
    return run(mp, ax, ay, bx, by, noc) ? 0 : 1;
  }
  HwPort hp;
  std::string err;
  if (!hp.open_dev("/dev/tenstorrent/" + std::to_string(dev), err)) {
    fprintf(stderr, "dev%d: %s\n", dev, err.c_str());
    return 1;
  }
  printf("== card %d (%s)\n", dev, g_go ? "GO" : "dry run");
  return run(hp, ax, ay, bx, by, noc) ? 0 : 1;
}
