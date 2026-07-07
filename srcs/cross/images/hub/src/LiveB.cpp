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

namespace MFM {

  struct FastB {
    GridManager mGridManager;
  };
  FAST_LOCAL(FastB,fB,b);

  // L1 DATA
  T6Grid theT6Grid[1];
  DLGridList theDLGridList;

  bool processHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    {
      static u32 spin = 0u;
      if ((++spin & 0x3f'ffff) == 0) {
        HBXTAG(hubPrcHC,spin);
        LOGXTAG(hubPrcHCL,spin);
      }
    }

    if (theEwpL1Data.isUninitted(ngbidx)) return false;

    if (!theEwpL1Data.isActive(ngbidx)) {
      HBPTAG(hPROCBLOC,ngbidx);
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

    HBASSERT_LS(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    //    HBPTAG(hub/INSIZ,car.currentTCSize());
    EwpPayload & pay = car.payload();

    //    LOGPTAG(EwpPay,carindex);
    fB.mGridManager.applyEWT(pay);

    //    HBPTAG(plCODE,(u32) pay.mPayloadState.mPayloadCode);
    u32 paysize = pay.currentPayloadSize();
    //    HBPTAG(plSize,paysize);
    car.closeTC(paysize); // ready to go
    //    HBPTAG(hub/AFTCLOS,car.isComplete());
    //    HBPTAG(hub/OUTSIZ,car.currentTCSize());
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    return true;
  }

  bool processInterHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    if (theInterHubL1Data.isUninitted(ngbidx))
      return false; // unconnected is not an error

    if (!theInterHubL1Data.isActive(ngbidx)) {
      HBPTAG(ihPROCBLOC,&theInterHubL1Data.getCarStg(ngbidx));
      HBPVAL(getNameFromEPState(theInterHubL1Data.getPublicEPState(ngbidx)));
      return false;             // wait a bit
    }

    //    HBPTAG(PROCINTERHUB,ngbidx);
    //    HBPTAG(pIHS,inside);

    using IHubData = T6EPL1Data<InterHubStorage,4>;
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[ngbidx];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    //    HBPTAG(PRINHU-crbi,&crbi);

    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars

    InterHubStorage & cars = theInterHubL1Data.mTheTCStorages[ngbidx];
    HBASSERT_LS(carindex, cars.getCarCount());
    InterHubBlock & car = cars.getTC(carindex);

    if (false) {
      HBXTAG(ihub/prcHC,car.getHeader().getU32());
      HBPTAG(tcmsiz,(u32) car.getHeader().mTCMSize);
      HBPTAG(paycap,TCMarker::decodeTCMSizeToPayloadCapacityBytes(car.getHeader().mTCMSize));
      HBPTAG(payovr,TCMarker::getPacketOverheadBytesForTCMSize(car.getHeader().mTCMSize));
      HBPTAG(pktszb,TCMarker::decodeTCMSizeToPacketSizeBytes(car.getHeader().mTCMSize));
      HBPTAG(pktwds,TCMarker::getPacketWordsFromTCMSize(car.getHeader().mTCMSize));
      HBPTAG(futidx,TCMarker::getFooterWordIndex(car.getHeader().mTCMSize));
      HBPTAG(aklidx,TCMarker::getAnkleWordIndex(car.getHeader().mTCMSize));
      HBPTAG(ngbidx,ngbidx);
      HBPTAG(caridx,carindex);
    }

    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    InterHubPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

    car.closeTC(sizeof(pay)); // ready to go
    //    HBPTAG(ihub/payat,&pay);
    //    HBPTAG(ihub/carat,&car);
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    //    HBPTAG(ihub/AFTCLOS,&crbo);
    
    return true;
  }

  int initB() {
    HBPTAG(INIT+,fAll.mNoC0);
    preloadT2Mailbox();
    theDLGridList.init();
    fB.mGridManager.init(theT6Grid[0],theACacheBlockL1Control,theDLGridList);
    // moved to T1 fB.mPACBControl.init(theACacheBlockL1Control,theDLGridList);
    HBPTAG(INIT-,fAll.mNoC0);
    return 0;
  }

  int liveB(HostBlock & hb) {
    HBNOTE("liveB");
    //hb.addBytes('L',hartChar(fAll.mHartNum));
    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    HBMARK;
    LOGMARK;

    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      const u32 BITS = 15;//16;
      const u32 LIM = (1<<BITS)-1;
      if ((++spin & LIM) == 0) {
        HBPTAG(horg,spin>>BITS); // generate some HB logging please?
        LOGPTAG(zorg,spin>>BITS); // generate SOME logging please?
        //        LOGPTAG(hub/liveB,spin); // generate SOME logging please?
        if (false) {
          static bool once;
          if (!once) theDLGridList.demo();
          once = true;
        }
      }
      if ((spin & 0x3ff) == 0)
        hb.hartbeat(fAll.mHartNum);

      bool work = false;
      for (u32 i = 0u; i < 4u; ++i) {
        if (processInterHubCars(i,hb,(i&1)==0)) {
          work = true;
        }
      }

      for (u32 e = 0u; e < 8u; ++e) {
        if (processHubCars(e,hb,true)) {
          work = true;
        }
      }

      //work = true;
      
      if (!work) {
        //breathe();
      }
    }
    return 0;
  }
}
