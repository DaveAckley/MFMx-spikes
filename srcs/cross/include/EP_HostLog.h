#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

#include "PT_HostLog.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 HLOG_BLOCKS = 4u;
  struct HostLogEP : public T6EP<HostLogEP,HostLogStorage,HLOG_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<HostLogEP,HostLogStorage,HLOG_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "HLOGi" : "HLOGo"; }
    u32 getCarSize() const { return sizeof(HostLogBlock); }
    HostLogBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LS(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(HostLogBlock & car, u8 carindex) ;

    void initHostLogEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

 
}
#include "EP_HostLog.tcc"
