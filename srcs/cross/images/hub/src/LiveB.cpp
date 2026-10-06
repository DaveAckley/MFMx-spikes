#include "StandardLife.h"
#include "HubLiveB.h"
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"
#include "EP_ACacheBlock.h"
#include "T6Phaser.h"
#include "TaskWorker.h"

namespace MFM {

  FAST_LOCAL(FastB,fB,b);

  // L1 DATA
  T6Grid theT6Grid[1];
  DLGridList theDLGridList;
  L1GridManagerControl theL1GridManagerControl;

  /*
  bool processInterHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    fB.mPIHControl.updateCars(ngbidx,hb,inside);
    EACH(1'000'000,HBPTAG(ihPROCAR,&theInterHubL1Data.getCarStg(ngbidx)));
    return false; // ??
  }
  */

  static bool processHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    {
      static u32 spin = 0u;
      if ((++spin & 0x7f'ffff) == 0) {
        HBXTAG(hubPrcHC,spin);
        LOGXTAG(hubPrcHCL,spin);
      }
    }

    if (theEwpL1Data.isUninitted(ngbidx)) return false;

    if (!theEwpL1Data.isActive(ngbidx)) {
      HBPTAG(hPROCBLOC,ngbidx);
      HBPVAL(getNameFromEPState(theEwpL1Data.getPublicEPState(ngbidx)));
      return false;             // maybe wait a bit
    }

    using EwpData = T6EPL1Data<EwpBlockStg,8>;
    EwpData::CarIdxs & idxs = theEwpL1Data.mTheCarIdxs[ngbidx];
    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[ngbidx];

    EwpData::CarIdxRB & crbi = idxs.mTheIdxs[EwpData::CarIdxs::COMM2COMP];
    EwpData::CarIdxRB & crbo = idxs.mTheIdxs[EwpData::CarIdxs::COMP2COMM];

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    {
      static u32 spin = 0u;
      if (((spin++) & 0xfffff) == 0)
        HBXTAG(hub_prcHC,spin);
    }

    HBASSERT_LT(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    EwpPayload & pay = car.payload();

    fB.mGridManager.applyEWT(pay,ngbidx);

    u32 paysize = pay.currentPayloadSize();
    car.closeTC(paysize); // ready to go
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    return true;
  }


  int myInitB() {
    HBPTAG(INIT+,fAll.mNoC0);
    theDLGridList.init();
    theL1GridManagerControl.init();
    fB.mGridManager.init(theT6Grid[0],theACacheBlockL1Control,theDLGridList);

    theInterHubL1Control.init();
    fB.mPIHControl.init(theInterHubL1Control);

    HBPTAG(INIT-,fAll.mNoC0);
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    HBMARK;
    LOGMARK;
    return 0;
  }

  void stepB(HostBlock & hb) {
    static u32 spin = 0u;

    const u32 BITS = 17;//15;
    const u32 LIM = (1<<BITS)-1;
    if ((++spin & LIM) == 0) {
      HBPTAG(stepB#,spin>>BITS); // generate some HB logging please?
    }
    if ((spin & 0x3ff) == 0)
      hb.hartbeat(fAll.mHartNum);

    // HANDLE EWPs
    for (u32 e = 0u; e < 8u; ++e) {
      processHubCars(e,hb,true);
    }

    // HANDLE IHubs
    fB.mPIHControl.stepB(hb);

  }
}
