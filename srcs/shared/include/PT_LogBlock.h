#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s

namespace MFM {

  struct LogBlockPayload {
    static constexpr u32 LBP_DATA_SIZE = 984;//1000;
    static constexpr u32 LBP_HIGH_BYTES_MARK = LBP_DATA_SIZE - 64;
    static constexpr u32 LBP_HIGH_TICKS_MARK = 10'000;
    static constexpr u32 LBP_HEADER_SIZE = 2+2+4;
    static constexpr u32 LBP_TOTAL_SIZE = LBP_DATA_SIZE + LBP_HEADER_SIZE;
    static constexpr u32 LBP_MAGIC = 0x11a2;
    u16 mMAGIC;
    u16 mDataUsed;
    u32 mTicksBase;
    u8 mData[LBP_DATA_SIZE];

    u32 getBytesRemaining() { return LBP_DATA_SIZE - mDataUsed; }
    bool put_u8(u8 byte) {
      if (mDataUsed >= LBP_DATA_SIZE) return false;
      mData[mDataUsed++] = byte;
      return true;
    }
    bool put_u16(u16 val) {
      if (mDataUsed >= LBP_DATA_SIZE-1) return false;
      mData[mDataUsed++] = val&0xff;
      mData[mDataUsed++] = val>>8;
      return true;
    }

    u32 getCurrentPayloadSize() {
      return 1u + (u32) (&mData[mDataUsed] - (u8*) this);
    }

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    void reset(u32 ticksbase) {
      mMAGIC = LBP_MAGIC;
      mDataUsed = 0;
      mTicksBase = ticksbase;
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
        mMAGIC == LBP_MAGIC &&
        mDataUsed <= LBP_DATA_SIZE;
    }
  };

  static_assert(sizeof(LogBlockPayload)==LogBlockPayload::LBP_TOTAL_SIZE,"Bad payload size");
  
  class LogBlock : public TC<LogBlock,sizeof(LogBlockPayload)> {
  public:
    const char * getName() const { return "LogBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    LogBlockPayload & payload() { return *(LogBlockPayload*) getDataStart(); }
    void init() {
      //HBNOTE(getName());
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      //HBNOTE(preCloseTC);
      //      payload().checksCheck();
      closeTC(sizeof(payload())); // and then close it, with a full load
      //HBNOTE(postCloseTC);
      //      payload().checksCheck();
      //HBNOTE(postSetDepTC);
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
      //      payload().checksCheck();
    }
  };

  constexpr u32 NUMBER_OF_LOGBLOCK_CARS = 3u;
  typedef TCStorage<LogBlock,NUMBER_OF_LOGBLOCK_CARS> LogBlockStg;

}
