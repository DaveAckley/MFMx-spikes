#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s
#include "AtomReport.h"

namespace MFM {

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
    static constexpr u16 ACBP_FLAG_RSRV1 =     0x0800;
    static constexpr u16 ACBP_FLAG_RSRV2 =     0x0400;

    u16 mFlagsAndLen;
    u8 mCData[ACBP_BUFFER_SIZE];

    bool addByte(u8 byte) {
      static bool wasblocked;
      if (getBytesRemaining() == 0) {
        if (!wasblocked) {
          HBPTAG(ACBblkt!,(u32)byte);
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
