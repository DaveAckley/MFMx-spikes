#include "StandardLife.h"
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
#include "T6Phaser.h" // for HTFuncPtr

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

    SNAP(5,HBPVAL(&crbo));
    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    EACH(10'000,HBPTAG(GOTEWT,(u32) carindex));

    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[0];
    HBASSERT_LT(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    EACH(10'000,HBPTAG(ewp/INSIZ,car.currentTCSize()));

    EwpPayload & pay = car.payload();
    bool ret = fB.mThePhysics.doTransition(pay);

    car.closeTC(pay.currentPayloadSize()); // ready to go
    EACH(10'000,HBPTAG(ewp/OUTSIZ,car.currentTCSize()));
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm

    return true;
  }

  int initB() {
    preloadT2Mailbox();
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    return 0;
  }

  int stepB(HostBlock & hb) {
    static u32 spin = 0u;
    if ((++spin & 0xfff) == 0) {
      if ((spin & 0xffffff) == 0) LOGXTAG(LiveB,spin);
      hb.hartbeat(fAll.mHartNum);
    }
    T6Phaser::handle();
    processEwpCars(hb,false);
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
      SNAP(2,HBPTAG(!EWPT!,getHartEpochName(hei)));
      return TEFR_NO_THANKS;
    }
    return TEFR_CONTINUE; // NOT REACHED for the moment
  }  
}
