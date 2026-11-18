#pragma once   /* -*- C++ -*- */
/** EthLinks.h -- host-side management of the Ethernet-tile E1 router
    cores (the eth-seam), ported from the srcs/host/tools/ethfwd*
    hardware tools (ERISC_NOTES.md, ERISC_SEAM_DESIGN.md).

    NO pybind and no MFMx runtime dependencies here (only itype.h and
    shared/EthSeam.h), so the logic can be tested natively with a mock
    EthPort (srcs/host/tools/ethlinks_test.cpp).  The pybind glue is
    EthSeamHostPy.h.

    What it manages, per Ethernet link (one ETH tile on each of two
    cards, joined by the QSFP-DD link):

      1. validate     read-only: tile is ETH, not harvested, link Up,
                      firmware chip-info valid, queue 2 / TX header 1
                      pristine
      2. queue 2      RXQ2 raw mode, TX header entry 1 (this tile's
                      MAC -> the FW-reported peer MAC), TXQ2 on,
                      header entry selected
      3. smoke test   64 B each way through queue 2 (both ends
                      configured), then the ring scratch is cleaned
      4. E1 deploy    image to L1 0x4000, CTL window zeroed, PC/END
                      set, tt-kmd NOC_CLEANUP armed (E1 returns to
                      reset if the owning fd closes, even on a crash),
                      E1's soft-reset bit released, ready word + image
                      id checked
      5. channels     per-channel config blocks (EthSeam.h layout),
                      ring headers zeroed, then go
      6. teardown     stop, E1 back in reset with its original PC/END
                      and soft-reset value, queue 2 restored to
                      pristine

    Every write goes through ONE allowlist (EthLinkEnd::writeOk); a
    write outside it is a programming error and aborts.

    The E1 does NOT survive the owning fd: tt-kmd's NOC_CLEANUP fires
    when the fd closes, so the object that owns the chip fd (MFMx: the
    Blackhole) must outlive the links.  That is why teardown is
    explicit AND the kernel cleanup is the safety net.

    NOT here (needs the T6-side wiring, ERISC_SEAM_DESIGN.md sections
    5, 12, 13, 15): deriving the per-channel T6 storage addresses from
    the image blocks.  This layer takes them as explicit
    EthChannelSetup input. */

#include "itype.h"
#include "EthSeam.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MFM {

  /** Access to the L1 and registers of ETH tiles (NoC0 x, y == 1) of
      ONE card. */
  struct EthPort {
    virtual ~EthPort() { }
    virtual bool rd32(u32 ethx, u32 addr, u32 & v) = 0;
    virtual bool wr32(u32 ethx, u32 addr, u32 v) = 0;

    /** Ask tt-kmd to write (addr,data) to tile (ethx,1) when the
        owning fd closes.  False if unavailable. */
    virtual bool setNocCleanup(u32 ethx, u32 addr, u32 data) {
      (void) ethx;
      (void) addr;
      (void) data;
      return false;
    }
  };

  /** EthPort over a tt-kmd device fd: a few lazily allocated 2 MiB
      TLB windows (uncached), freed in the destructor.  The fd stays
      owned by the caller (the Blackhole); this class never closes
      it. */
  class KmdEthPort : public EthPort {
  public:
    explicit KmdEthPort(int fd) : mFD(fd) { }
    ~KmdEthPort() override ;
    bool rd32(u32 ethx, u32 addr, u32 & v) override ;
    bool wr32(u32 ethx, u32 addr, u32 v) override ;
    bool setNocCleanup(u32 ethx, u32 addr, u32 data) override ;

  private:
    struct Win {
      u32 ethx;
      u64 base;
      u32 id;
      volatile u8 * ptr;
    };
    volatile u32 * map(u32 ethx, u32 addr) ;
    // Two windows per ETH tile (its L1 and its register page), and a
    // chip in the 2x2 array has up to eight links: 16, plus margin.
    static constexpr u32 MAX_WINDOWS = 24u;
    int mFD;
    std::vector<Win> mWins;
  };

  /** Where cars arrive for one channel and what the E1 does with
      them (see e1fwd13.c header). */
  struct EthChannel {
    u8 channel;     //< channel number on this E1 (0..NCH_MAX-1)
    u8 legs;        //< EthSeam::LEG_OUT | LEG_IN_NOC | LEG_IN_COUNT
    u8 t6x;         //< NoC0 x of the LOCAL T6 receiving this channel
    u8 t6y;         //< NoC0 y of that T6 (used by LEG_IN_NOC)
    u32 t6Base;     //< its receive storage: slot i at t6Base + i*CAR_BYTES
  };

  struct EthChannelSetup {
    u8 mode = EthSeam::E1_MODE_ROUTER;
    u8 flags = EthSeam::E1F_CSR;      //< L0 data cache off (CSR 0x7c0)
    u8 ringCars = EthSeam::RING_CARS; //< ring depth 1..4 = cars in flight
    u8 niuBuf = 0;                    //< NIU cmd buffer (0 = default 3)

    std::vector<EthChannel> channels;
  };

  struct EthHealth {
    u32 port = 0;
    u32 heartbeat = 0;
    u32 retrain = 0;
    u32 fecCorrected = 0;
    u32 fecUncorrected = 0;
  };

  /** Pluggable log sink (default: stderr).  The MFMx glue points it
      at BHLog. */
  void ethLinksSetLogger(std::function<void(const std::string &)> sink) ;

  /** How long validate() waits for a link to report Up (port status 1)
      before giving up, in milliseconds.  After a chip reset the base
      firmware retrains each link in the background, and one that is a
      little late used to fail the whole bring-up.  Default 60000;
      native tests set it small. */
  void ethLinksSetUpWaitMs(u32 ms) ;
  u32 ethLinksUpWaitMs() ;

  /** One end of a link: the ETH tile (ethx, 1) of one card. */
  class EthLinkEnd {
  public:
    EthLinkEnd(EthPort & port, u32 chip, u32 ethx)
      : mPort(port),
        mChip(chip),
        mEthX(ethx)
    { }

    /** read-only; fills MACs; checks pristine unless force.  waitMs < 0
        uses ethLinksUpWaitMs(); the link must report Up within that
        time (polled every 100 ms). */
    bool validate(bool force, int waitMs = -1) ;

    /** Poll the port status until it is 1 (Up) or waitMs ran out.  port
        is the last status read, waited the milliseconds spent. */
    bool waitPortUp(u32 waitMs, u32 & port, u32 & waited) ;

    /// needs validate()
    bool configureQueue2() ;

    bool deployE1(const std::vector<u32> & image, bool force) ;

    /// after deployE1 (E1 idle, waiting for go)
    bool configureChannels(const EthChannelSetup & cs) ;

    bool startE1() ;

    /// stop word + wait for done; go cleared
    bool stopE1() ;

    /// E1 back in reset, PC/END and soft-reset as found
    void resetE1() ;

    void restoreQueue2() ;

    /// 64 B this -> peer through queue 2, verified; both queues
    /// configured
    bool smokeSendTo(EthLinkEnd & peer) ;

    EthHealth health() ;

    /// read a channel-block word (stats)
    u32 channelStat(u32 channel, u32 wordIndex) ;

    u32 chip() const { return mChip; }
    u32 ethX() const { return mEthX; }
    bool isDeployed() const { return mDeployed; }
    bool isQueue2Configured() const { return mQ2; }
    const std::string & lastError() const { return mErr; }
    std::string name() const ;

    // identity from validate()
    u32 mMajor = 0;
    u32 mMinor = 0;
    u32 mPeerMajor = 0;
    u32 mPeerMinor = 0;

  private:
    bool R(u32 addr, u32 & v) ;
    u32 R(u32 addr) ;
    bool W(u32 addr, u32 v, bool verify = true) ;
    bool writeOk(u32 addr, u32 v) const ;
    bool fail(const std::string & why) ;

    EthPort & mPort;
    u32 mChip;
    u32 mEthX;
    std::string mErr;
    bool mValidated = false;
    bool mQ2 = false;
    bool mDeployed = false;
    bool mStarted = false;
    u32 mSrOrig = 0xFFFFFFFFu;
    u32 mPcOrig = 0;
    u32 mEndOrig = 0;
    u32 mImageBytes = 0;
  };

  /** Both ends of one link, with the whole bring-up / teardown
      sequence. */
  class EthLink {
  public:
    EthLink(EthPort & portA,
            u32 chipA,
            EthPort & portB,
            u32 chipB,
            u32 ethx)
      : mA(portA, chipA, ethx),
        mB(portB, chipB, ethx)
    { }

    /** validate both, check they are each other's FW-reported
        peers, queue 2 on both, smoke test both ways, deploy E1 on
        both, channel setup on both, go on both.  On failure
        everything already touched is torn down again and
        lastError() says why. */
    bool bringUp(const std::vector<u32> & image,
                 const EthChannelSetup & setupA,
                 const EthChannelSetup & setupB,
                 bool force = false,
                 bool doSmoke = true) ;

    /// stop both, E1s back in reset, queue 2 restored; safe to
    /// call twice
    void tearDown() ;

    bool isUp() const { return mUp; }
    EthLinkEnd & a() { return mA; }
    EthLinkEnd & b() { return mB; }
    const std::string & lastError() const { return mErr; }

  private:
    EthLinkEnd mA;
    EthLinkEnd mB;
    bool mUp = false;
    std::string mErr;
  };

  /** Read a flat little-endian E1 image (<= 0x4000 bytes, multiple
      of 4) built by srcs/host/tools/e1fwd/build.sh. */
  bool ethLoadImageFile(const std::string & path,
                        std::vector<u32> & words,
                        std::string & err) ;

} // namespace MFM
