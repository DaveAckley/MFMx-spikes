#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s
#include "AtomReport.h"

namespace MFM {

  /* What the host writes into mCData of a car it returns (6 of the 8 payload
     bytes an empty car's packet carries anyway), for the hub to log when it
     is asked to restart: where the host's decoder stands. */
  struct ACBHostFeedback {
    u16 mExpectedSpin;          //< spin the host expects of the next report
    u16 mPacketSeq;             //< packets taken from this hub so far (low 16)
    u16 mConsecInvalid;         //< bad reports in a row (saturating)
  };
  static_assert(sizeof(ACBHostFeedback) == 6, "feedback fits an empty car");

  struct ACacheBlockPayload {

    static constexpr u16 ACBP_BUFFER_SIZE = 990; // for ~1KB final packets
    static constexpr u16 ACBP_TOTAL_SIZE = ACBP_BUFFER_SIZE + 2; 

    static constexpr u8 ACBP_LEN_BITS = 10; // for max 1KB compressed bytes/packet
    static constexpr u16 ACBP_LEN_MASK = (1<<ACBP_LEN_BITS)-1;
    static constexpr u16 ACBP_FLAGS_MASK = U16_MAX ^ ACBP_LEN_MASK;
    static constexpr u16 ACBP_FLAG_HAS_FLAGS = 0x8000;
    static constexpr u16 ACBP_FLAG_IS_SOF =    0x4000; //< reset rb before reading
    static constexpr u16 ACBP_FLAG_IS_EOF =    0x2000;
    static constexpr u16 ACBP_FLAG_CLOSABLE =  0x1000; //< ready to close even if not full
    /* Stream restart handshake (the compressed report stream has no resync:
       one lost byte garbles everything after it).  HOST -> HUB, in a car the
       host returns (an empty car; the flags word rides along in its 16 byte
       packet): RESTART_REQ says "my decoder is desynced, restart the stream";
       EPOCH toggles for each new request.  HUB -> HOST: the first car of the
       restarted stream has SOF (the host resets its decoder on it) and EPOCH
       echoing the request being answered; the last car of the old stream has
       EOF|CLOSABLE.  The host keeps REQ set until it sees the matching SOF
       (level, not edge: a late or lost return cannot hurt). */
    static constexpr u16 ACBP_FLAG_RESTART_REQ = 0x0800;
    static constexpr u16 ACBP_FLAG_EPOCH =       0x0400;

    u16 mFlagsAndLen;
    u8 mCData[ACBP_BUFFER_SIZE];

    bool addByte(u8 byte) {
      static bool wasblocked;
      if (getBytesRemaining() == 0) {
        if (!wasblocked) {
          HBXTAG(ACBblkt!,(u32)byte);
          wasblocked = true;
        }
        EACH(10'000,HBPTAG(ACBABF,__EACHNUM__));
        return false;
      }
      if (wasblocked) {
        HBPTAG(ACBunbk!,(u32)byte);
        wasblocked = false;
      }
      mCData[getCurrentLength()] = byte;
      mFlagsAndLen++; // "len can't overflow into flags"
      return true;
    }

    u8 getByteOrDie(u32 index) const {
      MFM_API_ASSERT(index < getCurrentLength(),ILLEGAL_ARGUMENT);
      return mCData[index];
    }

    bool checkRCloseFlag() const {
      return (mFlagsAndLen & ACBP_FLAG_CLOSABLE);
    }

    bool setRCloseFlag() {
      bool ret = checkRCloseFlag();
      if (!ret) mFlagsAndLen |= ACBP_FLAG_CLOSABLE;
      return ret;
    }

    u32 getCurrentLength() const { return mFlagsAndLen & ACBP_LEN_MASK; }

    u32 getCurrentFlags() const { return mFlagsAndLen & ACBP_FLAGS_MASK; }

    u32 getBytesRemaining() const { return ACBP_BUFFER_SIZE - getCurrentLength(); }

    u32 getCurrentPayloadSize() const {
      return &mCData[getCurrentLength()] - (u8*) this;
    }

    void setFeedback(const ACBHostFeedback & fb) {
      const u8 * src = (const u8 *) &fb;
      for (u32 i = 0u; i < sizeof(fb); ++i) mCData[i] = src[i];
    }

    ACBHostFeedback getFeedback() const {
      ACBHostFeedback fb;
      u8 * dst = (u8 *) &fb;
      for (u32 i = 0u; i < sizeof(fb); ++i) dst[i] = mCData[i];
      return fb;
    }

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    void reset() {
      mFlagsAndLen = 0;
    }

    bool isValid() const {
      return
        getCurrentFlags() != 0u &&
        getCurrentLength() < ACBP_BUFFER_SIZE;
    }
  };
  
  static_assert(sizeof(ACacheBlockPayload)==ACacheBlockPayload::ACBP_TOTAL_SIZE,"Bad payload size");
  
  class ACacheBlock : public TC<ACacheBlock,sizeof(ACacheBlockPayload)> {
  public:
    // TCBase API
    const char * getName() const { return "ACacheBlock"; }
    const char * formatTC(EPBase & epb) { return getName(); } //< PLACEHOLDER

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }

    ACacheBlockPayload & payload() { return *(ACacheBlockPayload*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      closeTC(sizeof(payload())); // and then close it, with a full load <- XXX? why not empty?
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

  constexpr u32 NUMBER_OF_ACACHEBLOCK_CARS = 5u;
  typedef TCStorage<ACacheBlock,NUMBER_OF_ACACHEBLOCK_CARS> ACacheBlockStg;

}
