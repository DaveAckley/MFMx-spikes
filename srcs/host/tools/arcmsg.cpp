// arcmsg.cpp -- send ONE harmless message to the Blackhole ARC tile (the
// System Management Controller) through its host message queue, and print
// the reply.  New tool (arcpeek stays strictly read-only).
//
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror arcmsg.cpp -o arcmsg
//   ./arcmsg --dev 0            TEST (0x90) then GET_AICLK (0x34), card 0
//   ./arcmsg --dev 0 --test     only the transport check (value + 1 echo)
//   ./arcmsg --dev 0 --aiclk    only GET_AICLK
//   ./arcmsg --mock             fake ARC with a queue processor, no hardware
//
// SAFETY.  The commands are a WHITELIST of two, both side-effect free in
// the firmware source (tt-zephyr-platforms, msgqueue.c, aiclk_ppm.c):
//   TEST      0x90  reply data[1] = request value + 1
//   GET_AICLK 0x34  reply data[1] = AICLK in MHz, data[2] = clock mode
//                   (1 uncontrolled, 2 PPM forced, 3 PPM unforced)
// There is deliberately NO way to send FORCE_AICLK, GO_BUSY or anything
// else from this tool: a change tool gets its own new name.
// RUN IT ONLY ON AN IDLE CARD (no mfmxctl, no tt-smi, no other tool): the
// kernel driver and tt-smi use the same queues, and there is no locking.
// Queue 3 is used by default (--queue N) to stay away from the others.
//
// HOW (tt-kmd blackhole.c, firmware msgqueue.c; read, now being run):
//   ARC tile NoC0 (8,0).  Scratch register 11 (0x80030400 + 4*11) holds the
//   address of a 4-word info block: [0] = base of the queues, [1] = slots |
//   (queues << 8) = 4 | (4 << 8).  Each queue is 72 words (288 bytes):
//     words 0..7    header: 0 req_wptr, 1 resp_rptr (host writes);
//                           4 req_rptr, 5 resp_wptr (ARC writes), 6 serial
//     words 8..39   4 request slots of 8 words (word 0 low byte = command)
//     words 40..71  4 response slots of 8 words (word 0 low byte = status)
//   Pointers count 0..7 (twice the slot count): equal = empty, a difference
//   of 4 = full; the slot is pointer % 4.
//   Host: write the 8 request words, bump req_wptr, READ IT BACK (the
//   doorbell is in another TLB window: a read keeps the order), write 0 to
//   ARC_MSI_FIFO (0x800B0000), wait for resp_wptr != resp_rptr, read the
//   slot, bump resp_rptr.
#pragma GCC diagnostic ignored "-Wunused-function"
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

constexpr unsigned ARC_X = 8;
constexpr unsigned ARC_Y = 0;
constexpr uint32_t RESET_SCRATCH_BASE = 0x80030400u;
inline uint32_t scratch(unsigned n) { return RESET_SCRATCH_BASE + 4u * n; }
constexpr unsigned SC_BOOT_STATUS = 2;
constexpr unsigned SC_MSG_QCB_PTR = 11;
constexpr uint32_t ARC_MSI_FIFO = 0x800B0000u;

constexpr unsigned NUM_QUEUES = 4;
constexpr unsigned QUEUE_SLOTS = 4;
constexpr unsigned PTR_WRAP = 2 * QUEUE_SLOTS;
constexpr unsigned MSG_WORDS = 8;
constexpr unsigned Q_HDR_WORDS = 8;
constexpr unsigned Q_WORDS = Q_HDR_WORDS + 2 * QUEUE_SLOTS * MSG_WORDS;
constexpr unsigned H_REQ_WPTR = 0;    // host writes
constexpr unsigned H_RESP_RPTR = 1;   // host writes
constexpr unsigned H_REQ_RPTR = 4;    // ARC writes
constexpr unsigned H_RESP_WPTR = 5;   // ARC writes
constexpr unsigned H_SERIAL = 6;      // ARC writes

// The whitelist.  Nothing else can be sent by this tool.
constexpr uint32_t MSG_GET_AICLK = 0x34;
constexpr uint32_t MSG_TEST = 0x90;

struct Port {
  virtual ~Port() {}
  virtual bool rd32(uint32_t addr, uint32_t& v) = 0;
  virtual bool wr32(uint32_t addr, uint32_t v) = 0;
};

struct HwPort : Port {
  struct Win {
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
  volatile uint32_t* map(uint32_t a, std::string& err) {
    if (a & 3u) {
      err = "unaligned address";
      return nullptr;
    }
    uint64_t base = (uint64_t)a & ~TLB_MASK;
    uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    for (Win& w : wins)
      if (w.base == base) return (volatile uint32_t*)(w.p + off);
    if (wins.size() >= 4) {
      err = "too many windows";
      return nullptr;
    }
    tt_allocate_tlb at;
    memset(&at, 0, sizeof at);
    at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) {
      err = std::string("ALLOCATE_TLB: ") + strerror(errno) +
            " (all the card's TLB windows in use, e.g. by mfmxctl?)";
      return nullptr;
    }
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) {
      err = std::string("mmap: ") + strerror(errno);
      return nullptr;
    }
    tt_configure_tlb c;
    memset(&c, 0, sizeof c);
    c.in.id = at.out.id;
    c.in.config.addr = base;
    c.in.config.x_start = ARC_X;
    c.in.config.x_end = ARC_X;
    c.in.config.y_start = ARC_Y;
    c.in.config.y_end = ARC_Y;
    c.in.config.noc = 0;
    c.in.config.mcast = 0;
    c.in.config.ordering = 1;   // strict
    if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) {
      err = std::string("CONFIGURE_TLB: ") + strerror(errno);
      return nullptr;
    }
    wins.push_back({base, at.out.id, (volatile uint8_t*)p});
    return (volatile uint32_t*)((volatile uint8_t*)p + off);
  }
  bool rd32(uint32_t a, uint32_t& v) override {
    volatile uint32_t* p = map(a, lasterr);
    if (!p) {
      fprintf(stderr, "  read 0x%08x failed: %s\n", a, lasterr.c_str());
      return false;
    }
    v = *p;
    return true;
  }
  bool wr32(uint32_t a, uint32_t v) override {
    volatile uint32_t* p = map(a, lasterr);
    if (!p) {
      fprintf(stderr, "  write 0x%08x failed: %s\n", a, lasterr.c_str());
      return false;
    }
    *p = v;
    return true;
  }
};

// ---- MOCK: a fake ARC with the queue layout and a message processor.
// All numbers are FAKE (AICLK 800, forced mode 3).
struct MockPort : Port {
  static constexpr uint32_t INFO = 0x10017a08u;
  static constexpr uint32_t QBASE = 0x10017b00u;
  std::map<uint32_t, uint32_t> m;
  unsigned doorbells = 0;
  MockPort() {
    m[scratch(SC_BOOT_STATUS)] = 0x5;     // ready_for_msg, hw_init 2
    m[scratch(SC_MSG_QCB_PTR)] = 0x10017a00u;
    m[0x10017a00u] = QBASE;
    m[0x10017a04u] = QUEUE_SLOTS | (NUM_QUEUES << 8);
  }
  uint32_t get(uint32_t a) {
    auto it = m.find(a);
    return it == m.end() ? 0u : it->second;
  }
  void process() {
    for (unsigned q = 0; q < NUM_QUEUES; ++q) {
      uint32_t b = QBASE + 4u * Q_WORDS * q;
      uint32_t reqw = get(b + 4 * H_REQ_WPTR);
      uint32_t reqr = get(b + 4 * H_REQ_RPTR);
      uint32_t resw = get(b + 4 * H_RESP_WPTR);
      while (reqw != reqr) {
        uint32_t rq = b + 4 * (Q_HDR_WORDS + (reqr % QUEUE_SLOTS) * MSG_WORDS);
        uint32_t rs = b + 4 * (Q_HDR_WORDS + QUEUE_SLOTS * MSG_WORDS +
                               (resw % QUEUE_SLOTS) * MSG_WORDS);
        uint32_t cmd = get(rq) & 0xff;
        for (unsigned i = 0; i < MSG_WORDS; ++i) m[rs + 4 * i] = 0;
        if (cmd == MSG_TEST) {
          m[rs + 4] = get(rq + 4) + 1;
          m[rs + 8] = get(b + 4 * H_SERIAL) + 1;
        } else if (cmd == MSG_GET_AICLK) {
          m[rs + 4] = 800;
          m[rs + 8] = 3;
        } else {
          m[rs] = 0xff;
        }
        reqr = (reqr + 1) % PTR_WRAP;
        resw = (resw + 1) % PTR_WRAP;
        m[b + 4 * H_SERIAL] = get(b + 4 * H_SERIAL) + 1;
      }
      m[b + 4 * H_REQ_RPTR] = reqr;
      m[b + 4 * H_RESP_WPTR] = resw;
    }
  }
  bool rd32(uint32_t a, uint32_t& v) override {
    v = get(a);
    return true;
  }
  bool wr32(uint32_t a, uint32_t v) override {
    if (a == ARC_MSI_FIFO) {
      ++doorbells;
      process();
      return true;
    }
    m[a] = v;
    return true;
  }
};

bool plausible_ptr(uint32_t p) {
  return p != 0 && p != 0xFFFFFFFFu && (p & 3u) == 0;
}

// Send one request (word 0 low byte = command; arg = word 1) on queue q and
// collect the response.  Refuses to touch a queue that is not empty.
bool transact(Port& port, uint32_t qbase, unsigned q, uint32_t cmd,
              uint32_t arg, uint32_t resp[MSG_WORDS], unsigned timeout_ms) {
  const uint32_t b = qbase + 4u * Q_WORDS * q;
  uint32_t reqw = 0, reqr = 0, resw = 0, resr = 0;
  if (!port.rd32(b + 4 * H_REQ_WPTR, reqw) ||
      !port.rd32(b + 4 * H_REQ_RPTR, reqr) ||
      !port.rd32(b + 4 * H_RESP_WPTR, resw) ||
      !port.rd32(b + 4 * H_RESP_RPTR, resr))
    return false;
  printf("  queue %u: req wptr %u rptr %u, resp wptr %u rptr %u\n", q,
         reqw, reqr, resw, resr);
  if (reqw >= PTR_WRAP || reqr >= PTR_WRAP || resw >= PTR_WRAP ||
      resr >= PTR_WRAP) {
    printf("  pointer out of range: queue state not understood; stop\n");
    return false;
  }
  if (reqw != reqr || resw != resr) {
    printf("  queue %u is not empty (someone else is using it?); stop\n", q);
    return false;
  }
  const uint32_t rq =
      b + 4 * (Q_HDR_WORDS + (reqw % QUEUE_SLOTS) * MSG_WORDS);
  for (unsigned i = 0; i < MSG_WORDS; ++i) {
    uint32_t w = i == 0 ? cmd : (i == 1 ? arg : 0u);
    if (!port.wr32(rq + 4 * i, w)) return false;
  }
  uint32_t nw = (reqw + 1) % PTR_WRAP;
  uint32_t back = 0;
  if (!port.wr32(b + 4 * H_REQ_WPTR, nw) ||
      !port.rd32(b + 4 * H_REQ_WPTR, back))   // read-back orders the doorbell
    return false;
  if (back != nw) {
    printf("  req wptr read back %u, wanted %u; stop\n", back, nw);
    return false;
  }
  if (!port.wr32(ARC_MSI_FIFO, 0)) return false;
  const auto t0 = std::chrono::steady_clock::now();
  for (;;) {
    uint32_t w = 0;
    if (!port.rd32(b + 4 * H_RESP_WPTR, w)) return false;
    if (w != resr) break;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    if (ms > (long)timeout_ms) {
      printf("  no response after %u ms (request left queued)\n",
             timeout_ms);
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const uint32_t rs = b + 4 * (Q_HDR_WORDS + QUEUE_SLOTS * MSG_WORDS +
                               (resr % QUEUE_SLOTS) * MSG_WORDS);
  for (unsigned i = 0; i < MSG_WORDS; ++i)
    if (!port.rd32(rs + 4 * i, resp[i])) return false;
  if (!port.wr32(b + 4 * H_RESP_RPTR, (resr + 1) % PTR_WRAP)) return false;
  return true;
}

bool run_card(Port& port, const char* label, unsigned q, bool do_test,
              bool do_aiclk) {
  printf("== %s  (ARC tile NoC0 (%u,%u), queue %u)\n", label, ARC_X, ARC_Y,
         q);
  uint32_t boot = 0, info = 0;
  if (!port.rd32(scratch(SC_BOOT_STATUS), boot) ||
      !port.rd32(scratch(SC_MSG_QCB_PTR), info))
    return false;
  printf("  ARC_BOOT_STATUS 0x%08x  ready_for_msg=%u\n", boot, boot & 1u);
  if (boot == 0xFFFFFFFFu || !(boot & 1u)) {
    printf("  ARC not ready for messages; stop\n");
    return false;
  }
  if (!plausible_ptr(info)) {
    printf("  queue info pointer 0x%08x looks invalid; stop\n", info);
    return false;
  }
  uint32_t qbase = 0, geom = 0;
  if (!port.rd32(info, qbase) || !port.rd32(info + 4, geom)) return false;
  printf("  queue info at 0x%08x: base 0x%08x geometry 0x%08x\n", info,
         qbase, geom);
  if (!plausible_ptr(qbase) ||
      geom != (QUEUE_SLOTS | (NUM_QUEUES << 8))) {
    printf("  geometry is not 4 queues of 4 slots; stop\n");
    return false;
  }
  uint32_t resp[MSG_WORDS];
  if (do_test) {
    const uint32_t v = 0x1234u;
    if (!transact(port, qbase, q, MSG_TEST, v, resp, 2000)) return false;
    printf("  TEST      status 0x%08x  data[1]=0x%x (sent 0x%x, want +1)"
           "  data[2]=%u  %s\n",
           resp[0], resp[1], v, resp[2],
           resp[1] == v + 1 ? "TRANSPORT OK" : "MISMATCH");
    if (resp[1] != v + 1) return false;
  }
  if (do_aiclk) {
    if (!transact(port, qbase, q, MSG_GET_AICLK, 0, resp, 2000))
      return false;
    static const char* const MODE[] = {"?", "uncontrolled", "PPM forced",
                                       "PPM unforced"};
    printf("  GET_AICLK status 0x%08x  AICLK %u MHz  mode %u (%s)\n",
           resp[0], resp[1], resp[2], resp[2] < 4 ? MODE[resp[2]] : "?");
    if ((resp[0] & 0xff) != 0) printf("  nonzero exit code\n");
  }
  return true;
}

void usage() {
  fprintf(stderr,
          "usage: arcmsg [--dev N] [--queue Q] [--test] [--aiclk] [--mock]\n"
          "  sends only TEST and/or GET_AICLK (both harmless); default "
          "both.\n  Idle card only.\n");
}

}  // namespace

int main(int argc, char** argv) {
  int dev = -1;
  unsigned q = 3;
  bool do_test = false, do_aiclk = false, mock = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--dev" && i + 1 < argc) dev = atoi(argv[++i]);
    else if (a == "--queue" && i + 1 < argc) q = (unsigned)atoi(argv[++i]);
    else if (a == "--test") do_test = true;
    else if (a == "--aiclk") do_aiclk = true;
    else if (a == "--mock") mock = true;
    else {
      usage();
      return 2;
    }
  }
  if (!do_test && !do_aiclk) do_test = do_aiclk = true;
  if (q >= NUM_QUEUES) {
    usage();
    return 2;
  }
  if (mock) {
    MockPort mp;
    printf("MOCK: all numbers below are FAKE\n");
    bool ok = run_card(mp, "mock card", q, do_test, do_aiclk);
    printf("mock doorbells: %u\n", mp.doorbells);
    return ok ? 0 : 1;
  }
  if (dev < 0) {
    fprintf(stderr, "arcmsg writes to the card: name one with --dev N\n");
    return 2;
  }
  HwPort hp;
  std::string err;
  if (!hp.open_dev("/dev/tenstorrent/" + std::to_string(dev), err)) {
    fprintf(stderr, "dev%d: %s\n", dev, err.c_str());
    return 1;
  }
  char label[32];
  snprintf(label, sizeof label, "card %d", dev);
  return run_card(hp, label, q, do_test, do_aiclk) ? 0 : 1;
}
