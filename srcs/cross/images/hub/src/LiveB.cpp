#include "DefaultLives.h" // for liveB
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"
#include "InterHub.h"
#include "Grid.h"
#include "EP_ACacheBlock.h"
#include "T6Phaser.h"
#include "TaskWorker.h"

namespace MFM {

  struct FastB {
    GridManager mGridManager;
  };
  FAST_LOCAL(FastB,fB,b);

  // L1 DATA
  T6Grid theT6Grid[1];
  DLGridList theDLGridList;
  L1GridManagerControl theL1GridManagerControl;

  bool processHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
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

  bool processInterHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    SNAP(20,{LOGPX(ngbidx);LOGXX((u32)inside);});
    if (theInterHubL1Data.isUninitted(ngbidx))
      return false; // unconnected is not an error

    if (!theInterHubL1Data.isActive(ngbidx)) {
      EACH(1'000'000,HBPTAG(ihPROCAR,&theInterHubL1Data.getCarStg(ngbidx)));
      EACH(1'000'000,HBPVAL(getNameFromEPState(theInterHubL1Data.getPublicEPState(ngbidx))));
      return false;             // wait a bit
    }

    SNAP(20,LOGPTAG(pIHCin11,(u32)inside));

    using IHubData = T6EPL1Data<InterHubStorage,4>;
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[ngbidx];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];

    TaskWorker::updateHartTasks(); //< FOR HART B
#if 0
    //// SPIKE
    TaskManager::TaskXFerRB & n2brb = theTaskManager.getXFerRB(HARTNUM_NC,HARTNUM_B);
    u8 tn;
    if (n2brb.peek(tn)) { // weavegotmale!
      LOGPTAG(TKMG_gotTask,tn);
      Task & t = theTaskManager.getTask(tn);
      LOGPX(t.mTaskType);
      LOGPX(t.mBArg1);
      LOGPX(t.mBArg2);
      n2brb.drop();
      theTaskManager.deleteTask(tn); // XXX DO WORK
    }
    
    memoryFence();
#endif
    
    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars

    SNAP(20,LOGPTAG(pIHCin12,(u32)carindex));

    InterHubStorage & cars = theInterHubL1Data.mTheTCStorages[ngbidx];
    HBASSERT_LT(carindex, cars.getCarCount());
    InterHubBlock & car = cars.getTC(carindex);

    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    InterHubPayload & pay = car.payload();
    EACH(1'000'000,LOGPTAG(pIHCin13,&pay));
    pay.update(inside); // kilroy was here
    char dirstr[2];
    dir4ToByteCodeStr(dirstr,(Dir4) ngbidx);
    SNAP(100,{LOGPTAG(IHUBdi,dirstr);/*LOGPTAG(IHUBac,pay.mOrigin);*/});

    car.closeTC(sizeof(pay)); // ready to go
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    
    return true;
  }

  int myInitB() {
    HBPTAG(INIT+,fAll.mNoC0);
    theDLGridList.init();
    theL1GridManagerControl.init();
    fB.mGridManager.init(theT6Grid[0],theACacheBlockL1Control,theDLGridList);
    // moved to T1 fB.mPACBControl.init(theACacheBlockL1Control,theDLGridList);
    HBPTAG(INIT-,fAll.mNoC0);
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    HBMARK;
    LOGMARK;
    return 0;
  }

  int stepB(HostBlock & hb) {
    static u32 spin = 0u;
    //T6Phaser::handle();

    const u32 BITS = 16;//15;
    const u32 LIM = (1<<BITS)-1;
    if ((++spin & LIM) == 0) {
      HBPTAG(horg,spin>>BITS); // generate some HB logging please?
    }
    if ((spin & 0x3ff) == 0)
      hb.hartbeat(fAll.mHartNum);

    for (u32 i = 0u; i < 4u; ++i) {
      processInterHubCars(i,hb,(i&1)==0);
    }

    for (u32 e = 0u; e < 8u; ++e) {
      processHubCars(e,hb,true);
    }

    return 0;
  }
}
