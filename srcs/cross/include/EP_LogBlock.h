#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

#include "PT_LogBlock.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 HLOG_BLOCKS = 1u;
  struct LogBlockEP : public T6EP<LogBlockEP,LogBlockStg,HLOG_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<LogBlockEP,LogBlockStg,HLOG_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "LOGi" : "LOGo"; }
    u32 getCarSize() const { return sizeof(LogBlock); }
    LogBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(LogBlock & car, u8 carindex) ;

    void initLogBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

  struct LogBlockL1Control {
    using TheL1Data = LogBlockEP::Super::L1Data;

    AtomicLock mLock;
    LogBlock * mCurrentLogBlock;
    u8 mCurrentCarIndex;        // valid whenever mCurrentLogBlock != 0
    u16 mLastTickOffset;
    u32 mBaseTicks;
    u32 mMarksMissed;

    void init() ;
    int step(HostBlock & hb) ;
    bool readyToClose() ;
    bool writeMark(u16 fileid, u16 lineno,const char * msg) ;
  private:
    void setupNewCar(TheL1Data::CarIdxRB & crbi) ; // CALLER HOLDS LOCK & crbi IS NON-EMPTY
  };

  extern T6EPL1Data<LogBlockStg,1u> theLogBlockL1Data;
  extern LogBlockL1Control theLogBlockL1Control;


}
