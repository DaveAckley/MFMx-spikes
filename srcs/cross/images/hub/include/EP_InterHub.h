#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

#include "IHHState.h"

#include "PT_InterHub.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 IHUB_BLOCKS = 8u;
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

  struct InterHubL1Control { // with state for IHUB_BLOCKS blocks
    using TheL1Data = InterHubEP::Super::L1Data;

    AtomicLock mIHL1Lock;
    IHHState mT6CurrentIHSt, mT6NextIHSt;  // controlled for T6 by HB::stepB()

    struct L1Hub1 {
      InterHubBlock * mCurrentInterHub;
      u8 mCurrentCarIndex;        // valid whenever mCurrentInterHub != 0
      u8 mBlockIndex;
      IHHState mL1HNextIHSt; // controlled for IHUB_BLOCK by.. whoever?

      void init(u8 idx) {
        memset_s(this,'\0',sizeof(*this));
        mBlockIndex = idx;
      }
    };

    L1Hub1 mL1HubControls[IHUB_BLOCKS];

    L1Hub1 & getL1Hub1(u8 idx) {
      MFM_API_ASSERT(idx<IHUB_BLOCKS,ILLEGAL_ARGUMENT);
      return mL1HubControls[idx];
    }

    void init() ;
    
    bool readyToClose() ;

    void setupNewCar(TheL1Data::CarIdxRB & crbi) ; // COMP-SIDE CALLER HOLDS LOCK & crbi IS NON-EMPTY
  };

  struct InterHubPrivateControl { // with state for IHUB_BLOCKS blocks

    using TheL1Data = InterHubEP::Super::L1Data;

    static constexpr u32 RUN_ON_HARTNUM = HARTNUM_B;

    InterHubL1Control * mIHL1Control;
    IHHState mPrivateState;

    void init(InterHubL1Control & acbl1) ;
    void stepB(HostBlock & hb) ;
    void updateCars(u32 ngbidx,HostBlock & hb,bool inside) ;
    int updateAllBlocks(HostBlock & hb) ;

  private:
    /// State machine helper methods
    InterHubL1Control & getL1() {
      MFM_API_ASSERT_NONNULL(mIHL1Control);
      return *mIHL1Control;
    }
    
  };  

  extern InterHubL1Control theInterHubL1Control;

}
