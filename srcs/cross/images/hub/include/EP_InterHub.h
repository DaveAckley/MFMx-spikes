#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

#include "PT_InterHub.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 IHUB_BLOCKS = 4u;
  struct InterHubEP : public T6EP<InterHubEP,InterHubStorage,IHUB_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<InterHubEP,InterHubStorage,IHUB_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "IHEPi" : "IHEPo"; }
    u32 getCarSize() const { return sizeof(InterHubBlock); }
    InterHubBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LT(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(InterHubBlock & car, u8 carindex) ;

    void initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

 
}
#include "EP_InterHub.tcc"
