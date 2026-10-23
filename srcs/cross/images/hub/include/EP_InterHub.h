#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"

//#include "IHHState.h"

#include "PT_InterHub.h"
#include "TCStorage.h"

#include "Grid.h" //< for L1GridManagerControl

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
    bool shipTC(SUBTC & car, u8 carindex) ;
    bool recvTC(InterHubBlock & car, u8 carindex) ;

    void initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

  struct InterHubL1Control { // with state for IHUB_BLOCKS blocks
    using TheL1Data = InterHubEP::Super::L1Data;

    AtomicLock mIHL1Lock;
    //IHHState mT6CurrentIHSt, mT6NextIHSt;  // controlled for T6 by HB::stepB()

    // Keep l1gridmanager ptr here to drive home the linkage.
    L1GridManagerControl * mL1GridManagerControlPtr;
    L1GridManagerControl & getL1GridManagerControl() ;

#if 0    
    struct L1Hub1 {
      InterHubBlock * mCurrentInterHub;
      u8 mCurrentCarIndex;        // valid whenever mCurrentInterHub != 0
      Dir8 mL1HubDir8;

      void init(u8 idx) {
        memset_s(this,'\0',sizeof(*this));
        mL1HubDir8 = (Dir8) idx;
      }
      void setupNewCar(TheL1Data::CarIdxRB & crbi) ;
      void dropCar() { mCurrentInterHub = nullptr; }
    };

    L1Hub1 mL1HubControls[IHUB_BLOCKS];

    L1Hub1 & getL1Hub1(u8 idx) {
      MFM_API_ASSERT(idx<IHUB_BLOCKS,ILLEGAL_ARGUMENT);
      return mL1HubControls[idx];
    }
#endif

    void init() ;
    
    bool readyToClose() ;

#if 0
    void setupNewCar(TheL1Data::CarIdxRB & crbi) { // COMP-SIDE CALLER HOLDS LOCK & crbi IS NON-EMPTY
      FAIL(INCOMPLETE_CODE);
    }
#endif
  };

  struct InterHubPrivateControl { // with state for IHUB_BLOCKS blocks

    using TheL1Data = InterHubEP::Super::L1Data;

    static constexpr u32 RUN_ON_HARTNUM = HARTNUM_B;

    InterHubL1Control * mIHL1Control;
    //IHHState mPrivateState;

    void init(InterHubL1Control & acbl1) ;
    void stepB(HostBlock & hb) ;
    void updateCarsB(u32 ngbidx,HostBlock & hb,bool inside) ;
    //int updateAllBlocks(HostBlock & hb) ;

  private:
    /// PHASE PROCESSING
    void _ucLeaderLOAD_CACHE_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    void _ucFollowerLOAD_CACHE_B(u32 ngbidx,
                                 GridManager & gm,
                                 TheL1Data::CarIdxRB & crbi,
                                 TheL1Data::CarIdxRB & crbo) ;

    void _ucFollowerCARRY_ON_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    void _ucLeaderSAVE_CACHE_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    void _ucFollowerSAVE_CACHE_B(u32 ngbidx,
                                 GridManager & gm,
                                 TheL1Data::CarIdxRB & crbi,
                                 TheL1Data::CarIdxRB & crbo) ;
    
    /// State machine helper methods
    InterHubL1Control & getL1() {
      MFM_API_ASSERT_NONNULL(mIHL1Control);
      return *mIHL1Control;
    }
    
  };  

  extern InterHubL1Control theInterHubL1Control;

}
