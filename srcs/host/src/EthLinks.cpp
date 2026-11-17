#include "EthLinks.h"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "Constants.h"     // TENSTORRENT_IOCTL_*
#include "TTKMDStuff.h"    // tenstorrent_allocate_tlb & friends

namespace MFM {
  using namespace EthSeam;

  namespace {
    // Register / L1 addresses of an Ethernet tile.
    // See srcs/host/tools/ERISC_NOTES.md sections 1, 2, 7.

    // Base-FW boot results: +4 is the port status, chip-info
    // records are at +4*240 (own) and +4*248 (peer).
    constexpr u32 BOOT_RESULTS = 0x0007CC00u;

    constexpr u32 SYSENG_HEARTBEAT = 0x0007CC70u;
    constexpr u32 SYSENG_RETRAIN = 0x0007CE00u;
    constexpr u32 SYSENG_FEC_CORR = 0x0007CE90u;
    constexpr u32 SYSENG_FEC_UNCORR = 0x0007CE98u;

    constexpr u32 NIU0 = 0xFFB20000u;
    constexpr u32 NOC_ENDPOINT_ID_OFF = 0x048u;
    constexpr u32 NIU_CFG_0_OFF = 0x100u;
    constexpr u32 NIU_CFG_0_HARVESTED = 1u << 12;

    constexpr u32 TXQ2 = 0xFFB92000u;
    constexpr u32 RXQ2 = 0xFFB96000u;
    constexpr u32 TXHDR1 = 0xFFB98280u;

    constexpr u32 SOFT_RESET = 0xFFB121B0u;
    constexpr u32 E1_PC = 0xFFB14008u;
    constexpr u32 E1_END = 0xFFB1400Cu;
    constexpr u32 SR_E1 = 0x1000u;

    // Queue-2 receive buffer.  Inside the syseng region, but
    // empirically free (ERISC_NOTES 7).
    constexpr u32 RXBUF_L1 = 0x7E000u;
    constexpr u32 SMOKE_BYTES = 64u;

    std::function<void(const std::string &)> gSink;

    void logf(const char * fmt, ...) {
      char buf[600];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof buf, fmt, ap);
      va_end(ap);
      if (gSink) gSink(buf);
      else fprintf(stderr, "[EthLinks] %s\n", buf);
    }

    void sleepUs(u32 us) {
      std::this_thread::sleep_for(std::chrono::microseconds(us));
    }

    // MAC byte-order convention of the TX header words ("reverse
    // byte order" per the docs; verified on hardware)
    void macWords(u32 major, u32 minor, u32 & w0, u32 & w1) {
      u32 o[6] = {
        (major >> 16) & 255u,
        (major >> 8) & 255u,
        major & 255u,
        (minor >> 16) & 255u,
        (minor >> 8) & 255u,
        minor & 255u
      };
      w0 = o[5] | (o[4] << 8) | (o[3] << 16) | (o[2] << 24);
      w1 = o[1] | (o[0] << 8);
    }
  }

  void ethLinksSetLogger(std::function<void(const std::string &)> sink) {
    gSink = sink;
  }

  bool ethLoadImageFile(const std::string & path,
                        std::vector<u32> & words,
                        std::string & err) {
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) {
      err = "cannot open E1 image " + path;
      return false;
    }
    std::vector<unsigned char> b;
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
      b.insert(b.end(), buf, buf + n);
    }
    fclose(f);
    if (b.empty() || (b.size() & 3u) != 0u || b.size() > 0x4000u) {
      err = "bad E1 image " + path + ": "
        + std::to_string(b.size())
        + " bytes (need a nonzero multiple of 4, at most 0x4000)";
      return false;
    }
    words.resize(b.size() / 4u);
    for (size_t i = 0; i < words.size(); ++i) {
      words[i] =
        ((u32) b[4 * i]) |
        ((u32) b[4 * i + 1] << 8) |
        ((u32) b[4 * i + 2] << 16) |
        ((u32) b[4 * i + 3] << 24);
    }
    return true;
  }

  //// KmdEthPort ////////////////////////////////////////////////////

  KmdEthPort::~KmdEthPort() {
    for (Win & w : mWins) {
      munmap((void *) w.ptr, 1u << 21);
      struct tenstorrent_free_tlb ft;
      memset(&ft, 0, sizeof ft);
      ft.in.id = w.id;
      (void) ioctl(mFD, TENSTORRENT_IOCTL_FREE_TLB, &ft);
    }
  }

  volatile u32 * KmdEthPort::map(u32 ethx, u32 addr) {
    if (addr & 3u) return nullptr;
    const u64 base = (u64) addr & ~(u64) ((1u << 21) - 1u);
    const u32 off = (u32) ((u64) addr & ((1u << 21) - 1u));
    for (Win & w : mWins) {
      if (w.ethx == ethx && w.base == base) {
        return (volatile u32 *) (w.ptr + off);
      }
    }
    if (mWins.size() >= MAX_WINDOWS) return nullptr;

    struct tenstorrent_allocate_tlb at;
    memset(&at, 0, sizeof at);
    at.in.size = 1u << 21;
    if (ioctl(mFD, TENSTORRENT_IOCTL_ALLOCATE_TLB, &at) < 0) {
      logf("ALLOCATE_TLB failed: %s", strerror(errno));
      return nullptr;
    }

    struct tenstorrent_configure_tlb cf;
    memset(&cf, 0, sizeof cf);
    cf.in.id = at.out.id;
    cf.in.config.addr = base;
    cf.in.config.x_start = (u16) ethx;
    cf.in.config.x_end = (u16) ethx;
    cf.in.config.y_start = (u16) ETH_NOC0_Y;
    cf.in.config.y_end = (u16) ETH_NOC0_Y;
    cf.in.config.noc = 0;
    cf.in.config.mcast = 0;

    struct tenstorrent_free_tlb ft;
    memset(&ft, 0, sizeof ft);
    ft.in.id = at.out.id;

    void * p = mmap(nullptr,
                    1u << 21,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    mFD,
                    (off_t) at.out.mmap_offset_uc);
    if (p == MAP_FAILED) {
      logf("mmap of ETH window failed: %s", strerror(errno));
      (void) ioctl(mFD, TENSTORRENT_IOCTL_FREE_TLB, &ft);
      return nullptr;
    }
    if (ioctl(mFD, TENSTORRENT_IOCTL_CONFIGURE_TLB, &cf) < 0) {
      logf("CONFIGURE_TLB failed: %s", strerror(errno));
      munmap(p, 1u << 21);
      (void) ioctl(mFD, TENSTORRENT_IOCTL_FREE_TLB, &ft);
      return nullptr;
    }
    mWins.push_back({ ethx, base, at.out.id, (volatile u8 *) p });
    return (volatile u32 *) ((volatile u8 *) p + off);
  }

  bool KmdEthPort::rd32(u32 ethx, u32 addr, u32 & v) {
    volatile u32 * p = map(ethx, addr);
    if (!p) return false;
    v = *p;
    return true;
  }

  bool KmdEthPort::wr32(u32 ethx, u32 addr, u32 v) {
    volatile u32 * p = map(ethx, addr);
    if (!p) return false;
    *p = v;
    return true;
  }

  bool KmdEthPort::setNocCleanup(u32 ethx, u32 addr, u32 data) {
    // tt-kmd SET_NOC_CLEANUP request
    struct {
      u32 argsz;
      u32 flags;
      u8 enabled;
      u8 x;
      u8 y;
      u8 noc;
      u32 reserved0;
      u64 addr;
      u64 data;
    } req;
    memset(&req, 0, sizeof req);
    req.argsz = sizeof req;
    req.enabled = 1;
    req.x = (u8) ethx;
    req.y = (u8) ETH_NOC0_Y;
    req.noc = 0;
    req.addr = addr;
    req.data = data;
    return ioctl(mFD, TENSTORRENT_IOCTL_SET_NOC_CLEANUP, &req) == 0;
  }

  //// EthLinkEnd ////////////////////////////////////////////////////

  std::string EthLinkEnd::name() const {
    return "bh" + std::to_string(mChip) + ":eth" + std::to_string(mEthX);
  }

  bool EthLinkEnd::fail(const std::string & why) {
    mErr = name() + ": " + why;
    logf("%s", mErr.c_str());
    return false;
  }

  bool EthLinkEnd::R(u32 addr, u32 & v) {
    return mPort.rd32(mEthX, addr, v);
  }

  u32 EthLinkEnd::R(u32 addr) {
    u32 v = 0xDEADDEADu;
    if (!mPort.rd32(mEthX, addr, v)) v = 0xDEADDEADu;
    return v;
  }

  // THE write allowlist: nothing else is ever written to an ETH
  // tile by this class.
  bool EthLinkEnd::writeOk(u32 a, u32 v) const {
    if (a & 3u) return false;

    if (a == SOFT_RESET) {
      return
        mSrOrig != 0xFFFFFFFFu &&
        (v == mSrOrig || v == (mSrOrig & ~SR_E1));
    }
    if (a == E1_PC) return v == E1_CODE || v == mPcOrig;
    if (a == E1_END) {
      return v == E1_CODE + mImageBytes || v == mEndOrig;
    }

    // E1 image window
    if (a >= E1_CODE && a < E1_CODE + 0x4000u) return true;
    // CTL + channel blocks
    if (a >= CTL_BASE && a < CTL_BASE + CTL_WINDOW) return true;
    // E1 rings
    if (a >= OUT_RING0 && a < IN_RING0 + NCH_MAX * RING_BYTES) {
      return true;
    }

    // TXQ2 CMD: L1-write packet only
    if (a == TXQ2 + 0x04) return v == 2u;
    // TXQ2 ctrl: off / TT-link
    if (a == TXQ2 + 0x00) return v == 0u || v == 1u;
    if (a == TXQ2 + 0x14) return true;
    if (a == TXQ2 + 0x18) return true;
    if (a == TXQ2 + 0x1c) return true;
    if (a == TXQ2 + 0x80) return true;
    if (a == TXQ2 + 0x84) return true;

    // RXQ2 ctrl: off / TT-link raw
    if (a == RXQ2 + 0x00) return v == 0u || v == 2u;
    if (a == RXQ2 + 0x0c) return true;
    if (a == RXQ2 + 0x10) return true;
    if (a == RXQ2 + 0x18) return true;

    if (a == TXHDR1 + 0x10) return true;
    if (a == TXHDR1 + 0x14) return true;
    if (a == TXHDR1 + 0x18) return true;
    if (a == TXHDR1 + 0x1c) return true;
    return false;
  }

  bool EthLinkEnd::W(u32 addr, u32 v, bool verify) {
    if (!writeOk(addr, v)) {
      logf("INTERNAL: %s write 0x%08x=0x%x is not allowlisted;"
           " aborting",
           name().c_str(),
           addr,
           v);
      abort();
    }
    if (!mPort.wr32(mEthX, addr, v)) {
      return fail("write failed at " + std::to_string(addr));
    }
    if (verify) {
      u32 b = 0;
      if (!R(addr, b) || b != v) {
        char buf[100];
        snprintf(buf,
                 sizeof buf,
                 "readback mismatch at 0x%08x:"
                 " wrote 0x%08x got 0x%08x",
                 addr,
                 v,
                 b);
        return fail(buf);
      }
    }
    return true;
  }

  bool EthLinkEnd::validate(bool force) {
    mValidated = false;
    u32 ep = R(NIU0 + NOC_ENDPOINT_ID_OFF);
    u32 cfg = R(NIU0 + NIU_CFG_0_OFF);
    if ((u16) (ep >> 8) != 0x0200u) {
      char b[80];
      snprintf(b,
               sizeof b,
               "not an ETH tile (endpoint id 0x%08x)",
               ep);
      return fail(b);
    }
    if (cfg & NIU_CFG_0_HARVESTED) return fail("tile is harvested");

    u32 port = R(BOOT_RESULTS + 4);
    if (port != 1u) {
      return fail("link not Up (port status "
                  + std::to_string(port) + ")");
    }
    const u32 r = BOOT_RESULTS + 4u * 240u;
    if (R(r + 28) != 1u || R(r + 32 + 28) != 1u) {
      return fail("firmware chip-info records not valid");
    }
    mMajor = R(r + 12);
    mMinor = R(r + 16);
    mPeerMajor = R(r + 32 + 12);
    mPeerMinor = R(r + 32 + 16);

    bool pristine =
      R(TXQ2) == 0 &&
      R(RXQ2) == 0 &&
      R(TXHDR1 + 0x10) == 0 &&
      R(TXHDR1 + 0x14) == 0 &&
      R(TXHDR1 + 0x18) == 0 &&
      R(TXHDR1 + 0x1c) == 0 &&
      R(TXQ2 + 0x80) == 0 &&
      R(TXQ2 + 0x84) == 0;
    logf("%s: MAC %06x:%06x peer(FW) %06x:%06x pristine=%s",
         name().c_str(),
         mMajor,
         mMinor,
         mPeerMajor,
         mPeerMinor,
         pristine ? "yes" : "NO");
    if (!pristine && !force) {
      return fail("queue 2 / TX header 1 already in use"
                  " (force to override)");
    }
    mValidated = true;
    return true;
  }

  bool EthLinkEnd::configureQueue2() {
    if (!mValidated) {
      return fail("configureQueue2 before a successful validate");
    }
    u32 sa0, sa1, da0, da1;
    macWords(mMajor, mMinor, sa0, sa1);
    macWords(mPeerMajor, mPeerMinor, da0, da1);

    // From here on restoreQueue2 must clean up, even after a
    // failure.
    mQ2 = true;

    if (!W(RXQ2 + 0x0c, RXBUF_L1 >> 4)) return false;
    if (!W(RXQ2 + 0x10, 0x100)) return false;
    if (!W(RXQ2 + 0x18, 0xe)) return false;
    if (!W(RXQ2 + 0x00, 2)) return false;

    if (!W(TXHDR1 + 0x10, sa0)) return false;
    if (!W(TXHDR1 + 0x14, sa1)) return false;
    if (!W(TXHDR1 + 0x18, da0)) return false;
    if (!W(TXHDR1 + 0x1c, da1)) return false;
    if (!W(TXQ2 + 0x84, 1)) return false;

    if (!W(TXQ2 + 0x00, 1)) return false;
    sleepUs(20000);

    // Header entry 1 (unicast).  NEVER send with entry 0
    // (broadcast -> base-FW RXQ0).
    return W(TXQ2 + 0x80, 1u << 8);
  }

  void EthLinkEnd::restoreQueue2() {
    if (!mQ2) return;
    (void) W(TXQ2 + 0x00, 0);
    (void) W(RXQ2 + 0x00, 0);
    (void) W(TXQ2 + 0x80, 0);
    (void) W(TXQ2 + 0x84, 0);
    (void) W(TXHDR1 + 0x10, 0);
    (void) W(TXHDR1 + 0x14, 0);
    (void) W(TXHDR1 + 0x18, 0);
    (void) W(TXHDR1 + 0x1c, 0);
    mQ2 = false;
  }

  bool EthLinkEnd::smokeSendTo(EthLinkEnd & peer) {
    if (!mQ2 || !peer.mQ2) {
      return fail("smoke test needs queue 2 configured"
                  " on both ends");
    }
    const u32 words = SMOKE_BYTES / 4u;
    const u32 tag = 0x5A000000u ^ (mChip << 20) ^ (mEthX << 12);

    for (u32 w = 0; w < words; ++w) {
      if (!W(OUT_RING0 + 4 * w, tag ^ w)) return false;
      if (!peer.W(IN_RING0 + 4 * w, 0xA5A5A5A5u)) return false;
    }
    // flush posted writes
    (void) R(OUT_RING0);
    (void) peer.R(IN_RING0);

    const u32 rb = peer.R(RXQ2 + 0x40);
    if (!W(TXQ2 + 0x18, SMOKE_BYTES)) return false;
    if (!W(TXQ2 + 0x80, 1u << 8)) return false;
    if (!W(TXQ2 + 0x14, OUT_RING0)) return false;
    if (!W(TXQ2 + 0x1c, IN_RING0)) return false;

    bool idle = false;
    for (int i = 0; i < 100000 && !idle; ++i) {
      idle = !(R(TXQ2 + 0x08) & 0x10000u);
    }
    if (!idle) return fail("TXQ2 busy before the smoke packet");
    if (!W(TXQ2 + 0x04, 2, false)) return false;

    bool arrived = false;
    for (int i = 0; i < 5000 && !arrived; ++i) {
      arrived = ((peer.R(RXQ2 + 0x40) - rb) & 0xffu) == 1u;
      if (!arrived) sleepUs(100);
    }
    sleepUs(5000);

    u32 bad = 0;
    for (u32 w = 0; w < words; ++w) {
      bad += (peer.R(IN_RING0 + 4 * w) != (tag ^ w));
    }
    // leave the rings clean
    for (u32 w = 0; w < words; ++w) {
      (void) W(OUT_RING0 + 4 * w, 0);
      (void) peer.W(IN_RING0 + 4 * w, 0);
    }
    if (!arrived) {
      return fail("smoke packet never arrived"
                  " (RX sequence did not advance) toward "
                  + peer.name());
    }
    if (bad) {
      return fail("smoke packet arrived with "
                  + std::to_string(bad)
                  + " wrong words toward "
                  + peer.name());
    }
    logf("%s -> %s smoke OK", name().c_str(), peer.name().c_str());
    return true;
  }

  bool EthLinkEnd::deployE1(const std::vector<u32> & image,
                            bool force) {
    if (image.empty() || image.size() > 0x1000u) {
      return fail("bad E1 image size");
    }
    mSrOrig = R(SOFT_RESET);
    mPcOrig = R(E1_PC);
    mEndOrig = R(E1_END);
    mImageBytes = (u32) image.size() * 4u;
    logf("%s: soft-reset 0x%08x (E1 %s) PC 0x%x END 0x%x",
         name().c_str(),
         mSrOrig,
         (mSrOrig & SR_E1) ? "in reset" : "RUNNING",
         mPcOrig,
         mEndOrig);
    if (!(mSrOrig & SR_E1) && !force) {
      mSrOrig = 0xFFFFFFFFu;
      return fail("E1 is not in reset"
                  " (someone else is using it; force to override)");
    }

    // From here on resetE1 must clean up, even after a failure.
    mDeployed = true;

    for (u32 a = CTL_BASE; a < CTL_BASE + CTL_WINDOW; a += 4) {
      if (!W(a, 0, false)) return false;
    }
    for (size_t i = 0; i < image.size(); ++i) {
      if (!W(E1_CODE + 4 * (u32) i, image[i], false)) return false;
    }
    u32 bad = 0;
    for (size_t i = 0; i < image.size(); ++i) {
      bad += (R(E1_CODE + 4 * (u32) i) != image[i]);
    }
    if (bad) {
      return fail("E1 image readback mismatch in "
                  + std::to_string(bad) + " words");
    }
    if (!W(E1_PC, E1_CODE)) return false;
    if (!W(E1_END, E1_CODE + mImageBytes)) return false;

    bool armed = mPort.setNocCleanup(mEthX, SOFT_RESET, mSrOrig);
    logf("%s: NOC_CLEANUP %s",
         name().c_str(),
         armed
         ? "armed"
         : "NOT available (old tt-kmd?);"
           " relying on explicit teardown");

    // release E1 only; other bits untouched
    if (!W(SOFT_RESET, mSrOrig & ~SR_E1, false)) return false;

    for (int i = 0; i < 2000; ++i) {
      if (R(CTL_BASE + CTL_READY) == E1_READY_WORD) {
        u32 id = R(CTL_BASE + CTL_IMAGE_ID);
        if (id < E1_IMAGE_ID_MIN || id > E1_IMAGE_ID_MAX) {
          char b[80];
          snprintf(b,
                   sizeof b,
                   "E1 up but image id 0x%08x"
                   " is not an e1fwd13..16 image",
                   id);
          return fail(b);
        }
        logf("%s: E1 up (image id 0x%08x)", name().c_str(), id);
        return true;
      }
      sleepUs(50);
    }
    return fail("E1 did not come up (no ready word)");
  }

  bool EthLinkEnd::configureChannels(const EthChannelSetup & cs) {
    if (!mDeployed) return fail("configureChannels before deployE1");
    if (cs.channels.empty()) return fail("no channels");

    u32 nch = 0;
    bool seen[NCH_MAX] = { false };
    for (const EthChannel & ch : cs.channels) {
      if (ch.channel >= NCH_MAX) {
        return fail("channel number "
                    + std::to_string(ch.channel)
                    + " >= NCH_MAX");
      }
      if (seen[ch.channel]) {
        return fail("duplicate channel "
                    + std::to_string(ch.channel));
      }
      seen[ch.channel] = true;
      if (ch.channel + 1u > nch) nch = ch.channel + 1u;

      // The E1 will NoC-write 4 cars of CAR_BYTES here: refuse
      // anything that is not plainly a Tensix L1 region.
      if (ch.legs & LEG_IN_NOC) {
        const bool xok =
          (ch.t6x >= 1 && ch.t6x <= 7) ||
          (ch.t6x >= 10 && ch.t6x <= 16);
        if (!xok || ch.t6y < 2 || ch.t6y > 11) {
          return fail("channel "
                      + std::to_string(ch.channel)
                      + ": T6 tile is not a Tensix tile");
        }
        if ((ch.t6Base & 15u) != 0u ||
            ch.t6Base < 0x30u ||
            (u64) ch.t6Base + RING_BYTES > 0x180000u) {
          return fail("channel "
                      + std::to_string(ch.channel)
                      + ": bad T6 base address");
        }
      }
    }
    if (cs.ringCars < 1 || cs.ringCars > RING_CARS) {
      return fail("ring depth must be 1.."
                  + std::to_string(RING_CARS));
    }

    // Stats, config, results: zero them all.  Not ready (+8) nor
    // the image id.
    for (u32 a = CTL_BASE + 0x28u; a < CTL_BASE + CTL_WINDOW; a += 4) {
      if (a != CTL_BASE + CTL_IMAGE_ID && !W(a, 0, false)) {
        return false;
      }
    }
    if (!W(CTL_BASE + CTL_STOP, 0)) return false;
    if (!W(CTL_BASE + CTL_DONE, 0)) return false;
    if (!W(CTL_BASE + CTL_NIU_BUF, cs.niuBuf)) return false;
    if (!W(CTL_BASE + CTL_MODE, cs.mode)) return false;
    if (!W(CTL_BASE + CTL_FLAGS, cs.flags)) return false;
    if (!W(CTL_BASE + CTL_NCH, nch)) return false;
    if (!W(CTL_BASE + CTL_VERIFY_EVERY, 0)) return false;
    if (!W(CTL_BASE + CTL_RING_CARS, cs.ringCars)) return false;
    if (!W(CTL_BASE + CTL_NODE_ID, (ETH_NOC0_Y << 6) | mEthX)) {
      return false;
    }

    for (const EthChannel & ch : cs.channels) {
      const u32 cb = channelBlockAddr(ch.channel);
      const u32 outRing = outRingAddr(ch.channel);
      const u32 inRing = inRingAddr(ch.channel);
      const u32 t6node = ((u32) ch.t6y << 6) | ch.t6x;
      if (!W(cb + 4 * CW_OUT, outRing)) return false;
      if (!W(cb + 4 * CW_IN, inRing)) return false;
      if (!W(cb + 4 * CW_PEERIN, inRing)) return false;
      if (!W(cb + 4 * CW_T6NODE, t6node)) return false;
      if (!W(cb + 4 * CW_T6BASE, ch.t6Base)) return false;
      if (!W(cb + 4 * CW_LEGS, ch.legs)) return false;

      // A zero header can never look like a complete car: the
      // rings MUST start clean.
      for (u32 i = 0; i < RING_CARS; ++i) {
        if (!W(outRing + i * CAR_BYTES, 0, false)) return false;
        if (!W(inRing + i * CAR_BYTES, 0, false)) return false;
      }
    }
    // flush before the go word
    (void) R(CTL_BASE);
    return true;
  }

  bool EthLinkEnd::startE1() {
    if (!mDeployed) return fail("startE1 before deployE1");
    (void) R(CTL_BASE + CTL_GO);
    if (!W(CTL_BASE + CTL_GO, 1)) return false;
    mStarted = true;
    return true;
  }

  bool EthLinkEnd::stopE1() {
    // An E1 that never got 'go' has nothing to acknowledge.
    if (!mDeployed || !mStarted) return true;
    mStarted = false;
    bool ok = W(CTL_BASE + CTL_STOP, 1);
    bool done = false;
    for (int i = 0; ok && i < 20000 && !done; ++i) {
      done = R(CTL_BASE + CTL_DONE) == 1u;
      if (!done) sleepUs(50);
    }
    (void) W(CTL_BASE + CTL_GO, 0);
    if (!done) return fail("E1 did not acknowledge the stop");
    return true;
  }

  void EthLinkEnd::resetE1() {
    if (!mDeployed) return;
    (void) W(SOFT_RESET, mSrOrig, false);
    (void) W(E1_PC, mPcOrig);
    (void) W(E1_END, mEndOrig);
    logf("%s: E1 back in reset (soft-reset now 0x%08x)",
         name().c_str(),
         R(SOFT_RESET));
    mDeployed = false;
  }

  u32 EthLinkEnd::channelStat(u32 channel, u32 wordIndex) {
    return R(channelBlockAddr(channel) + 4 * wordIndex);
  }

  EthHealth EthLinkEnd::health() {
    EthHealth h;
    h.port = R(BOOT_RESULTS + 4);
    h.heartbeat = R(SYSENG_HEARTBEAT);
    h.retrain = R(SYSENG_RETRAIN);
    h.fecCorrected = R(SYSENG_FEC_CORR);
    h.fecUncorrected = R(SYSENG_FEC_UNCORR);
    return h;
  }

  //// EthLink ///////////////////////////////////////////////////////

  bool EthLink::bringUp(const std::vector<u32> & image,
                        const EthChannelSetup & setupA,
                        const EthChannelSetup & setupB,
                        bool force,
                        bool doSmoke) {
    mErr.clear();
    if (mUp) {
      mErr = "link already up";
      return false;
    }
    auto bad = [&](EthLinkEnd & e) {
      mErr = e.lastError();
      tearDown();
      return false;
    };

    if (!mA.validate(force)) return bad(mA);
    if (!mB.validate(force)) return bad(mB);
    if (mA.mPeerMajor != mB.mMajor ||
        mA.mPeerMinor != mB.mMinor ||
        mB.mPeerMajor != mA.mMajor ||
        mB.mPeerMinor != mA.mMinor) {
      mErr = mA.name() + " and " + mB.name()
        + " are not each other's firmware-reported peers";
      logf("%s", mErr.c_str());
      return false;
    }

    if (!mA.configureQueue2()) return bad(mA);
    if (!mB.configureQueue2()) return bad(mB);
    if (doSmoke) {
      if (!mA.smokeSendTo(mB)) return bad(mA);
      if (!mB.smokeSendTo(mA)) return bad(mB);
    }

    if (!mB.deployE1(image, force)) return bad(mB);
    if (!mA.deployE1(image, force)) return bad(mA);
    if (!mB.configureChannels(setupB)) return bad(mB);
    if (!mA.configureChannels(setupA)) return bad(mA);
    if (!mB.startE1()) return bad(mB);
    if (!mA.startE1()) return bad(mA);

    mUp = true;
    logf("link %s <-> %s is up",
         mA.name().c_str(),
         mB.name().c_str());
    return true;
  }

  void EthLink::tearDown() {
    (void) mA.stopE1();
    (void) mB.stopE1();
    mA.resetE1();
    mB.resetE1();
    mA.restoreQueue2();
    mB.restoreQueue2();
    mUp = false;
  }

} // namespace MFM
