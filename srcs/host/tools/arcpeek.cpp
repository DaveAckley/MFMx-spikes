// arcpeek.cpp -- READ-ONLY look at the Blackhole ARC tile (the System Management Controller): boot status and
// the telemetry table, above all the current AI clock (AICLK).  New tool; changes nothing on the chip.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror arcpeek.cpp -o arcpeek
//   ./arcpeek                  every card (/dev/tenstorrent/0..15 that opens): status + all telemetry tags
//   ./arcpeek --dev 0 --aiclk  one card, just the clock lines
//   ./arcpeek --mock           fake chip, no hardware (all numbers FAKE; exercises the table walk)
//
// HOW (from tt-kmd blackhole.c and the tt-system-firmware telemetry docs; checked by reading, not yet by running):
//   ARC tile = NoC0 (8,0).  reset-unit scratch registers RESET_SCRATCH(N) = 0x80030400 + 4N:
//     N=2 ARC_BOOT_STATUS (bit0 READY_FOR_MSG, bits2:1 HW_INIT, 1 = started), N=4 ARC_ERROR_STATUS0 (bit1 cable fault),
//     N=11 ARC_MSG_QCB_PTR, N=12 ARC_TELEMETRY_DATA (data base), N=13 ARC_TELEMETRY_PTR (table header).
//   Telemetry table at PTR: word0 version (major = bits 23:16), word1 entry count, then entry_count u32 tag entries
//   from PTR+8: low 16 bits = tag id, high 16 bits = offset in 4-byte units from the data base.  Value = u32 at
//   data_base + 4*offset.  Tag ids from tt-kmd telemetry.h (below).
//   The AICLK word's layout is NOT documented in what we read: we print it raw and, as a HINT only, its low 16 bits
//   (expected to be the current clock in MHz) and high 16 bits.
//   Host window: one 2 MiB tt-kmd TLB per 2 MiB of ARC address space touched (like the ethfwd tools).  READS ONLY:
//   no ARC message is sent, no doorbell, no register written.  If mfmxctl is running it may hold most of the card's
//   202 windows; then ALLOCATE_TLB fails and this says so.
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

// ---- tt-kmd ioctl ABI (as in ethfwd22.cpp / MFMx srcs/host/include/TTKMDStuff.h) ----
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
constexpr uint64_t TLB_SIZE = 1ull << 21;
constexpr uint64_t TLB_MASK = TLB_SIZE - 1;

constexpr unsigned ARC_X = 8, ARC_Y = 0;
constexpr uint32_t RESET_SCRATCH_BASE = 0x80030400u;
inline uint32_t scratch(unsigned n) { return RESET_SCRATCH_BASE + 4u * n; }
constexpr unsigned SC_BOOT_STATUS = 2, SC_ERROR_STATUS0 = 4, SC_MSG_QCB_PTR = 11, SC_TELEMETRY_DATA = 12, SC_TELEMETRY_PTR = 13;

struct TagName { unsigned id; const char* name; };
const TagName TAGS[] = {   // tt-kmd telemetry.h (decimal ids)
  {1, "BOARD_ID"}, {6, "VCORE"}, {7, "POWER"}, {8, "CURRENT"}, {9, "VDD_LIMITS"}, {10, "THM_LIMIT_SHUTDOWN"},
  {11, "ASIC_TEMP"}, {14, "AICLK"}, {15, "AXICLK"}, {16, "ARCCLK"}, {24, "ETH_FW_VERSION"}, {26, "BM_APP_FW_VERSION"},
  {27, "BM_BL_FW_VERSION"}, {28, "FLASH_BUNDLE_VERSION"}, {29, "CM_FW_VERSION"}, {31, "FAN_SPEED"},
  {32, "TIMER_HEARTBEAT"}, {41, "FAN_RPM"}, {55, "TDC_LIMIT_MAX"}, {56, "THM_LIMIT_THROTTLE"}, {58, "TT_FLASH_VERSION"},
  {60, "THERM_TRIP_COUNT"}, {61, "ASIC_ID"}, {63, "AICLK_LIMIT_MAX"}, {64, "TDP_LIMIT_MAX"},
};
const char* tag_name(unsigned id) {
  for (const TagName& t : TAGS) if (t.id == id) return t.name;
  return "?";
}
constexpr unsigned TAG_AICLK = 14, TAG_TIMER_HEARTBEAT = 32, TAG_AICLK_LIMIT_MAX = 63;

// ---- a port reads u32 words of the ARC tile (8,0) ----
struct Port {
  virtual ~Port() {}
  virtual bool rd32(uint32_t addr, uint32_t& v) = 0;
};

struct HwPort : Port {
  struct Win { uint64_t base; uint32_t id; volatile uint8_t* p; };
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
  volatile uint32_t* map(uint32_t a, std::string& err) {
    if (a & 3u) { err = "unaligned address"; return nullptr; }
    uint64_t base = (uint64_t)a & ~TLB_MASK; uint32_t off = (uint32_t)((uint64_t)a & TLB_MASK);
    for (Win& w : wins) if (w.base == base) return (volatile uint32_t*)(w.p + off);
    if (wins.size() >= 4) { err = "too many windows"; return nullptr; }
    tt_allocate_tlb at; memset(&at, 0, sizeof at); at.in.size = TLB_SIZE;
    if (ioctl(fd, TT_IOCTL_ALLOCATE_TLB, &at) < 0) { err = std::string("ALLOCATE_TLB: ") + strerror(errno) + " (are all the card's TLB windows in use, e.g. by mfmxctl?)"; return nullptr; }
    void* p = mmap(nullptr, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)at.out.mmap_offset_uc);
    if (p == MAP_FAILED) { err = std::string("mmap: ") + strerror(errno); return nullptr; }
    tt_configure_tlb c; memset(&c, 0, sizeof c);
    c.in.id = at.out.id; c.in.config.addr = base;
    c.in.config.x_start = ARC_X; c.in.config.x_end = ARC_X; c.in.config.y_start = ARC_Y; c.in.config.y_end = ARC_Y;
    c.in.config.noc = 0; c.in.config.mcast = 0; c.in.config.ordering = 1;
    if (ioctl(fd, TT_IOCTL_CONFIGURE_TLB, &c) < 0) { err = std::string("CONFIGURE_TLB: ") + strerror(errno); return nullptr; }
    wins.push_back({base, at.out.id, (volatile uint8_t*)p});
    return (volatile uint32_t*)((volatile uint8_t*)p + off);
  }
  std::string lasterr;
  bool rd32(uint32_t a, uint32_t& v) override {
    volatile uint32_t* p = map(a, lasterr);
    if (!p) { fprintf(stderr, "  read 0x%08x failed: %s\n", a, lasterr.c_str()); return false; }
    v = *p;
    return true;
  }
};

// ---- MOCK: a fake ARC with a tiny telemetry table (numbers are FAKE) ----
struct MockPort : Port {
  std::map<uint32_t, uint32_t> m;
  uint32_t hb = 1000;
  MockPort() {
    const uint32_t table = 0x10001000u, data = 0x10001100u;
    m[scratch(SC_BOOT_STATUS)] = 0x1;            // READY_FOR_MSG, HW_INIT not started
    m[scratch(SC_ERROR_STATUS0)] = 0;
    m[scratch(SC_MSG_QCB_PTR)] = 0x10002000u;
    m[scratch(SC_TELEMETRY_DATA)] = data;
    m[scratch(SC_TELEMETRY_PTR)] = table;
    const unsigned ids[] = {1, 14, 32, 63, 11};
    const uint32_t vals[] = {0x00010001u, 800u | (1350u << 16), 0, 1400u, 47u};
    m[table] = 0x00010000u;                       // version 1.0
    m[table + 4] = 5;
    for (unsigned i = 0; i < 5; ++i) { m[table + 8 + 4 * i] = ids[i] | (i << 16); m[data + 4 * i] = vals[i]; }
  }
  bool rd32(uint32_t a, uint32_t& v) override {
    if (a == 0x10001100u + 4 * 2) hb += 100;     // the heartbeat moves
    auto it = m.find(a);
    v = it == m.end() ? 0u : it->second;
    if (a == 0x10001100u + 4 * 2) v = hb;
    return true;
  }
};

bool plausible_ptr(uint32_t p) { return p != 0 && p != 0xFFFFFFFFu && (p & 3u) == 0; }

// Walk one card.  aiclk_only: print only the clock lines.
bool peek(Port& port, const char* label, bool aiclk_only) {
  printf("== %s  (ARC tile NoC0 (%u,%u), READ-ONLY)\n", label, ARC_X, ARC_Y);
  uint32_t boot = 0, err0 = 0, qcb = 0, tdata = 0, tptr = 0;
  if (!port.rd32(scratch(SC_BOOT_STATUS), boot) || !port.rd32(scratch(SC_ERROR_STATUS0), err0) ||
      !port.rd32(scratch(SC_MSG_QCB_PTR), qcb) || !port.rd32(scratch(SC_TELEMETRY_DATA), tdata) ||
      !port.rd32(scratch(SC_TELEMETRY_PTR), tptr))
    return false;
  if (boot == 0xFFFFFFFFu) { printf("  ARC_BOOT_STATUS reads all ones: the ARC is not answering (card hung or in reset?)\n"); return false; }
  if (!aiclk_only) {
    printf("  ARC_BOOT_STATUS   0x%08x  ready_for_msg=%u hw_init=%u%s\n", boot, boot & 1u, (boot >> 1) & 3u,
           ((boot >> 1) & 3u) == 1u ? " (STARTED: firmware still initialising)" : "");
    printf("  ARC_ERROR_STATUS0 0x%08x%s\n", err0, (err0 & 2u) ? "  (CABLE FAULT bit set)" : "");
    printf("  ARC_MSG_QCB_PTR   0x%08x   ARC_TELEMETRY_DATA 0x%08x   ARC_TELEMETRY_PTR 0x%08x\n", qcb, tdata, tptr);
  }
  if (!plausible_ptr(tptr) || !plausible_ptr(tdata)) { printf("  telemetry pointers look invalid; stopping\n"); return false; }
  uint32_t ver = 0, count = 0;
  if (!port.rd32(tptr, ver) || !port.rd32(tptr + 4, count)) return false;
  unsigned major = (ver >> 16) & 0xff;
  if (!aiclk_only) printf("  telemetry table: version 0x%08x (major %u)  entries %u\n", ver, major, count);
  if (major > 1) { printf("  telemetry major version %u is newer than this tool understands (1); stopping\n", major); return false; }
  if (count == 0 || count > 256) { printf("  implausible entry count %u; stopping\n", count); return false; }
  struct Ent { unsigned id, off; uint32_t val; };
  std::vector<Ent> ents;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t e = 0, v = 0;
    if (!port.rd32(tptr + 8 + 4 * i, e)) return false;
    unsigned id = e & 0xffff, off = e >> 16;
    if (!port.rd32(tdata + 4u * off, v)) return false;
    ents.push_back({id, off, v});
  }
  bool saw_aiclk = false;
  for (const Ent& e : ents) {
    bool clock = e.id == TAG_AICLK || e.id == TAG_AICLK_LIMIT_MAX;
    if (aiclk_only && !clock) continue;
    printf("  tag %3u %-22s offset %3u  raw 0x%08x = %u", e.id, tag_name(e.id), e.off, e.val, e.val);
    if (e.id == TAG_AICLK) {
      saw_aiclk = true;
      printf("   [hint: low16 = %u (MHz?), high16 = %u]", e.val & 0xffff, e.val >> 16);
    }
    printf("\n");
  }
  if (!aiclk_only) {   // is the ARC alive?  The heartbeat tag should move.
    for (const Ent& e : ents) if (e.id == TAG_TIMER_HEARTBEAT) {
      uint32_t a = 0, b = 0;
      port.rd32(tdata + 4u * e.off, a);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      port.rd32(tdata + 4u * e.off, b);
      printf("  heartbeat %u -> %u over 200 ms: %s\n", a, b, a != b ? "ARC alive" : "NOT MOVING");
    }
  }
  if (!saw_aiclk) printf("  (no AICLK tag in this table)\n");
  return true;
}

void usage() {
  fprintf(stderr, "usage: arcpeek [--dev N] [--aiclk] [--mock]\n"
                  "  READ-ONLY: boot status and telemetry of the Blackhole ARC tile; --aiclk prints only the clock tags.\n");
}

}  // namespace

int main(int argc, char** argv) {
  int dev = -1;
  bool aiclk = false, mock = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--dev" && i + 1 < argc) dev = atoi(argv[++i]);
    else if (a == "--aiclk") aiclk = true;
    else if (a == "--mock") mock = true;
    else { usage(); return 2; }
  }
  if (mock) {
    MockPort mp;
    printf("MOCK: all numbers below are FAKE\n");
    return peek(mp, "mock card", aiclk) ? 0 : 1;
  }
  int done = 0, bad = 0;
  for (int d = 0; d < 16; ++d) {
    if (dev >= 0 && d != dev) continue;
    HwPort hp;
    std::string err;
    if (!hp.open_dev("/dev/tenstorrent/" + std::to_string(d), err)) {
      if (dev >= 0) { fprintf(stderr, "dev%d: %s\n", d, err.c_str()); ++bad; }
      continue;
    }
    char label[32];
    snprintf(label, sizeof label, "card %d", d);
    if (peek(hp, label, aiclk)) ++done; else ++bad;
  }
  if (done == 0 && bad == 0) fprintf(stderr, "no Blackhole cards found under /dev/tenstorrent\n");
  return (done > 0 && bad == 0) ? 0 : 1;
}
