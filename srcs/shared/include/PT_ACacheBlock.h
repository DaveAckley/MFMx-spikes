#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "Debug.h"
#include "TCStorage.h"
#include "XUtils.h" // for memset_s

namespace MFM {

  struct AtomReport {
    P4Atom mAtom;
    U16C mCoord;
  };

  struct ACacheBlockPayload {
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
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

  constexpr u32 NUMBER_OF_ACACHEBLOCK_CARS = 3u;
  typedef TCStorage<ACacheBlock,NUMBER_OF_ACACHEBLOCK_CARS> ACacheBlockStg;

}
