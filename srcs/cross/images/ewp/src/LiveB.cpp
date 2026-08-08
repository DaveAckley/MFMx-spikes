#include "DefaultLives.h" // for liveB
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"
#include "Physics.h"
#include "HartTasks.h" // for HTFuncPtr

namespace MFM {

  struct FastB {
    Physics mThePhysics;
    U8C mHubCP;
  };
  FAST_LOCAL(FastB,fB,b);

  bool processEwpCars(HostBlock & hb,bool inside) {
    if (!theEwpL1Data.isActive(0)) {
      SNAP(3,HBPTAG(ePROCBLOC,theEwpL1Data.getPublicEPState(0)));
      return false;             // wait a bit
    }

    using EwpData = T6EPL1Data<EwpBlockStg,1>;
    EwpData::CarIdxs & idxs = theEwpL1Data.mTheCarIdxs[0];
    EwpData::CarIdxRB & crbi = idxs.mTheIdxs[EwpData::CarIdxs::COMM2COMP];
    EwpData::CarIdxRB & crbo = idxs.mTheIdxs[EwpData::CarIdxs::COMP2COMM];

    //    SNAP(5,HBPVAL(&crbo));
    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    //    HBPTAG(GOTEWT,(u32) carindex);

    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[0];
    HBASSERT_LS(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    //    HBPTAG(ewp/INSIZ,car.currentTCSize());

    EwpPayload & pay = car.payload();
    bool ret = fB.mThePhysics.doTransition(pay);

    car.closeTC(pay.currentPayloadSize()); // ready to go
    //    HBPTAG(ewp/OUTSIZ,car.currentTCSize());
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm

    return true;
  }

  int initB() {
    preloadT2Mailbox();
    return 0;
  }

  int liveB(HostBlock & hb) {
    MFM_API_ASSERT(hb.goodMagic(),ILLEGAL_STATE);
    HBPTAG(@,__FUNCTION__);

    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop

    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xfff) == 0) {
        if ((spin & 0xffffff) == 0) LOGPTAG(LiveB,spin);
        hb.hartbeat(fAll.mHartNum);
      }
      if (!processEwpCars(hb,false))
        breathe();
    }
    return 0;
  }

  RCFlag manageEwpDemoT0(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      HBMARK;
    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {
      static u32 spin = 0;
      //// LIFE
      static constexpr u32 BITS = 13;
      if ((spin++ & ((1u<<BITS)-1)) == 0) {
        HBPTAG(ewpdemo,spin>>BITS);
        LOGPTAG(ewpdemo,spin>>BITS);
      }
    } else LOGPTAG(unknown htoc,htoc);
    return ret;
  }
  __attribute__((section(".rodata_fp_table_t0")))
  HTFuncPtr demoT0 = &manageEwpDemoT0;


  //////// SPECIAL DEMO INIT TASK
  TEFResult TaskEpochFunction_EWPT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    switch (hei) {

    default:
      HBPTAG(!EWPT!,getHartEpochName(hei));
      break;
    }
    return TEFR_CONTINUE;
  }  
}
