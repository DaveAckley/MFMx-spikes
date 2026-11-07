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

  struct InterHubPrivateControl { // with state for IHUB_BLOCKS blocks

    using TheL1Data = InterHubEP::Super::L1Data;

    static constexpr u32 RUN_ON_HARTNUM = HARTNUM_B;

    void init() ;
#if 0
    void stepB(HostBlock & hb) ;
    void updateIHCarsB(u32 ngbidx,HostBlock & hb,bool inside) ;
    //int updateAllBlocks(HostBlock & hb) ;
#endif

  private:
#if 0
    /// PHASE PROCESSING: returns true iff done, false if should go again 
    bool _ucLeaderLOAD_CACHE_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    bool _ucFollowerLOAD_CACHE_B(u32 ngbidx,
                                 GridManager & gm,
                                 TheL1Data::CarIdxRB & crbi,
                                 TheL1Data::CarIdxRB & crbo) ;

    bool _ucFollowerRUN_EVENTS_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    bool _ucLeaderSAVE_CACHE_B(u32 ngbidx,
                               GridManager & gm,
                               TheL1Data::CarIdxRB & crbi,
                               TheL1Data::CarIdxRB & crbo) ;

    bool _ucFollowerSAVE_CACHE_B(u32 ngbidx,
                                 GridManager & gm,
                                 TheL1Data::CarIdxRB & crbi,
                                 TheL1Data::CarIdxRB & crbo) ;
    
    /// State machine helper methods
#endif    
  };  

}
