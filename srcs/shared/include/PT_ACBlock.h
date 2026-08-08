#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s
#include "AtomReport.h"

namespace MFM {

  struct ACBlockPayload {

    static constexpr u16 ACBlockP_BUFFER_SIZE = 994; // for 1KB final packets
    static constexpr u16 ACBlockP_TOTAL_SIZE = ACBlockP_BUFFER_SIZE + 2; 

    static constexpr u8 ACBlockP_LEN_BITS = 10; // for max 1KB compressed bytes/packet
    static constexpr u16 ACBlockP_LEN_MASK = (1<<ACBlockP_LEN_BITS)-1;
    static constexpr u16 ACBlockP_FLAGS_MASK = U16_MAX ^ ACBlockP_LEN_MASK;
    static constexpr u16 ACBlockP_FLAG_HAS_FLAGS = 0x8000;
    static constexpr u16 ACBlockP_FLAG_IS_SOF =    0x4000; //< reset rb before reading
    static constexpr u16 ACBlockP_FLAG_IS_EOF =    0x2000;
    static constexpr u16 ACBlockP_FLAG_CLOSABLE =  0x1000; //< ready to close even if not full
    static constexpr u16 ACBlockP_FLAG_RSRV1 =     0x0800;
    static constexpr u16 ACBlockP_FLAG_RSRV2 =     0x0400;

    u16 mFlagsAndLen;
    u8 mCData[ACBlockP_BUFFER_SIZE];

    bool addByte(u8 byte) {
      if (getBytesRemaining() == 0) return false;
      mCData[getCurrentLength()] = byte;
      mFlagsAndLen++; // "len can't overflow into flags"
      return true;
    }

    bool checkRCloseFlag() const {
      return (mFlagsAndLen & ACBlockP_FLAG_CLOSABLE);
    }

    bool setRCloseFlag() {
      bool ret = checkRCloseFlag();
      if (!ret) mFlagsAndLen |= ACBlockP_FLAG_CLOSABLE;
      return ret;
    }

    u32 getCurrentLength() const { return mFlagsAndLen & ACBlockP_LEN_MASK; }

    u32 getCurrentFlags() const { return mFlagsAndLen & ACBlockP_FLAGS_MASK; }

    u32 getBytesRemaining() const { return ACBlockP_BUFFER_SIZE - getCurrentLength(); }

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
        getCurrentLength() < ACBlockP_BUFFER_SIZE;
    }
  };
  

  static_assert(sizeof(ACBlockPayload)==ACBlockPayload::ACBlockP_TOTAL_SIZE,"Bad payload size");
  
  class ACBlock : public TC<ACBlock,sizeof(ACBlockPayload)> {
  public:
    const char * getName() const { return "ACBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }

    ACBlockPayload & payload() { return *(ACBlockPayload*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      closeTC(sizeof(payload())); // and then close it, with a full load <- XXX? why not empty?
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

  constexpr u32 NUMBER_OF_ACBlock_CARS = 3u;
  typedef TCStorage<ACBlock,NUMBER_OF_ACBlock_CARS> ACBlockStg;

}
