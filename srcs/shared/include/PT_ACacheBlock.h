#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s
#include "AtomReport.h"

namespace MFM {

  struct ACacheBlockPayload {

    static constexpr u16 ACBP_BUFFER_SIZE = 994; // for 1KB final packets
    static constexpr u16 ACBP_TOTAL_SIZE = ACBP_BUFFER_SIZE + 2; 

    static constexpr u8 ACBP_LEN_BITS = 10; // for max 1KB compressed bytes/packet
    static constexpr u16 ACBP_LEN_MASK = (1<<ACBP_LEN_BITS)-1;
    static constexpr u16 ACBP_FLAGS_MASK = U16_MAX ^ ACBP_LEN_MASK;
    static constexpr u16 ACBP_FLAG_HAS_FLAGS = 0x8000;
    static constexpr u16 ACBP_FLAG_IS_SOF =    0x4000; //< reset rb before reading
    static constexpr u16 ACBP_FLAG_IS_EOF =    0x2000;
    static constexpr u16 ACBP_FLAG_CLOSABLE =  0x1000; //< ready to close even if not full
    static constexpr u16 ACBP_FLAG_RSRV1 =     0x0800;
    static constexpr u16 ACBP_FLAG_RSRV2 =     0x0400;

    u16 mFlagsAndLen;
    u8 mCData[ACBP_BUFFER_SIZE];

    bool addByte(u8 byte) {
      if (getBytesRemaining() == 0) return false;
      mCData[getCurrentLength()] = byte;
      mFlagsAndLen++; // "len can't overflow into flags"
      return true;
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
      return 2u + getCurrentLength();
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
  struct ACacheBlockPayloadOLD {
    static constexpr u32 ACBP_TOTAL_SIZE = 1444u;
    static constexpr u32 ACBP_MAX_TICKS = 100u;

    static constexpr u16 ACBP_MAGIC = 0xacbd;
    static constexpr u8 ACBP_MAX_REPORTS = 90;
    static constexpr u8 ACBP_HIGH_REPORTS_MARK = 2*ACBP_MAX_REPORTS/3;

    u16 mMAGIC;
    u8 mReportCount, mRSRV;
    AtomReport mReports[ACBP_MAX_REPORTS];

    u32 getReportsRemaining() { return ACBP_MAX_REPORTS - mReportCount; }
    bool put_report(P4Atom atom, U16C coord) {
      if (getReportsRemaining() == 0) {
        LOGPTAG(ptREP0,mReportCount);
        return false;
      }
      AtomReport & ar = mReports[mReportCount++];
      ar.mAtom = atom;
      ar.mCoord = coord;
      return true;
    }

    u32 getCurrentPayloadSize() {
      return 1u + (u32) (((u8*) &mReports[mReportCount]) - (u8*) this);
    }

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    void reset() {
      mMAGIC = ACBP_MAGIC;
      mReportCount = 0;
    }

    bool update(bool inside) { 
      if (!isValid()) {
        return false;
      }
      FAIL(INCOMPLETE_CODE);
      return true;
    }

    bool isValid() const {
      return
        mMAGIC == ACBP_MAGIC &&
        mReportCount < ACBP_MAX_REPORTS;
    }
  };
  
  class ACacheBlock : public TC<ACacheBlock,sizeof(ACacheBlockPayload)> {
  public:
    const char * getName() const { return "ACacheBlock"; }

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

  constexpr u32 NUMBER_OF_ACACHEBLOCK_CARS = 3u;
  typedef TCStorage<ACacheBlock,NUMBER_OF_ACACHEBLOCK_CARS> ACacheBlockStg;

}
