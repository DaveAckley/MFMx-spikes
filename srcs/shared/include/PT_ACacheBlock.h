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

  /* Seal-time self check: 1 recomputes the Fletcher sum over the car's bytes
     at seal and compares it with the running (shadow) sum, to catch memory
     damage between the write and the seal (.zn/.zw/.zx, mShadowBad).  Costs
     a pass over up to 1KB per car; off by default, for hunting that bug.
     Build with -DACBP_SEAL_VERIFY=1 to turn it on. */
#ifndef ACBP_SEAL_VERIFY
#define ACBP_SEAL_VERIFY 0
#endif

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

    /* Per packet integrity trailer.  The hub appends it when it closes a car:
       u16 packet sequence number, u16 checksum of the data bytes before it
       (both little endian).  It rides at the end of the data, counted in the
       length, and the compressor never gets to fill that space.  The host
       checks it to tell a packet damaged in transit (checksum, or a missing
       or repeated packet: sequence) from a stream broken before it was
       packetized (trailer fine, decoder desyncs anyway). */
    static constexpr u16 ACBP_TRAILER_SIZE = 4;
    static constexpr u16 ACBP_DATA_SIZE = ACBP_BUFFER_SIZE - ACBP_TRAILER_SIZE;

    u16 mFlagsAndLen;
    u8 mCData[ACBP_BUFFER_SIZE];

    static u16 checksum(const u8 * data, u32 len) { // Fletcher-16
      u32 a = 0u, b = 0u;
      for (u32 i = 0u; i < len; ++i) {
        a = (a + data[i]) % 255u;
        b = (b + a) % 255u;
      }
      return (u16) ((b << 8) | a);
    }

    /** One Fletcher-16 step; the hub folds each byte in as the sink accepts
        it, so the trailer sum is of what was MEANT to be stored. */
    static void fletcherStep(u16 & a, u16 & b, u8 byte) {
      a = (u16) ((a + byte) % 255u);
      b = (u16) ((b + a) % 255u);
    }

    /** Hub, at close: append the trailer (counts toward the length).  The
        sum is the SHADOW sum kept at write time (a,b), not recomputed from
        the car, so memory damage between write and host read shows up as a
        bad checksum.  Returns false if it differs from the car's current
        contents (damage before sealing). */
    bool sealTrailer(u16 seq, u16 a, u16 b) {
      u32 len = getCurrentLength();
      if (len + ACBP_TRAILER_SIZE > ACBP_BUFFER_SIZE) return true; // cannot happen
      u16 sum = (u16) ((b << 8) | a);
#if ACBP_SEAL_VERIFY
      bool same = sum == checksum(mCData,len);
#else
      const bool same = true;   // no recompute at seal (see ACBP_SEAL_VERIFY)
#endif
      mCData[len+0u] = (u8) (seq & 0xff);
      mCData[len+1u] = (u8) (seq >> 8);
      mCData[len+2u] = (u8) (sum & 0xff);
      mCData[len+3u] = (u8) (sum >> 8);
      mFlagsAndLen += ACBP_TRAILER_SIZE;  // len cannot overflow into flags
      return same;
    }

    /** Host: false if there is no room for a trailer or the checksum fails;
        seq is set when there is a trailer. */
    bool checkTrailer(u16 & seq) const {
      u32 len = getCurrentLength();
      if (len < ACBP_TRAILER_SIZE) return false;
      u32 dlen = len - ACBP_TRAILER_SIZE;
      seq = (u16) (mCData[dlen] | (mCData[dlen+1u] << 8));
      u16 sum = (u16) (mCData[dlen+2u] | (mCData[dlen+3u] << 8));
      return sum == checksum(mCData,dlen);
    }

    /** Host: the compressed bytes, not counting the trailer. */
    u32 getDataLength() const {
      u32 len = getCurrentLength();
      return len >= ACBP_TRAILER_SIZE ? len - ACBP_TRAILER_SIZE : 0u;
    }

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

    u32 getBytesRemaining() const { // data space left (trailer excluded)
      u32 len = getCurrentLength();
      return len >= ACBP_DATA_SIZE ? 0u : ACBP_DATA_SIZE - len;
    }

    u32 getCurrentPayloadSize() const {
      return &mCData[getCurrentLength()] - (u8*) this;
    }

    void setFeedback(const ACBHostFeedback & fb) {
      const u8 * src = (const u8 *) &fb;
      for (u32 i = 0u; i < sizeof(fb); ++i) mCData[i] = src[i];
    }

    /* REFRESH REQUESTS: in a returned empty car whose RESTART_REQ flag is
       clear, the same 6 bytes carry up to REFRESH_SLOTS sites the host
       wants re-reported, as (x+1,y+1) in the hub's grid coords; 0 = unused
       (so a zeroed car asks for nothing). */
    static constexpr u32 REFRESH_SLOTS = 3u;
    void setRefreshSlot(u32 i, U8C c) {
      mCData[2u*i] = (u8) (c.x + 1u);
      mCData[2u*i+1u] = (u8) (c.y + 1u);
    }
    bool getRefreshSlot(u32 i, U8C & c) const {
      if (i >= REFRESH_SLOTS || mCData[2u*i] == 0u || mCData[2u*i+1u] == 0u)
        return false;
      c = U8C((u8) (mCData[2u*i] - 1u), (u8) (mCData[2u*i+1u] - 1u));
      return true;
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
