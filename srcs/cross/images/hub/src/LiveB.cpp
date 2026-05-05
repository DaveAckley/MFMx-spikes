#include "DefaultLives.h" // for liveB
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"
#include "InterHub.h"

namespace MFM {

  struct FastB {
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
  };
  FAST_LOCAL(FastB,fB,b);

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  static bool updateFastEW(HostBlock & hb) {
    EventWindow & ew = fB.mFastEW;
    P4Atom & ca = ew.mAtoms[0];
    u32 cat = ca.getType();
    ////// BEGIN "PHYSICS" //////
    switch (cat) {
    default: {
      // wth are you? 
      ca = P4Atom::makeEmptyAtom(); // buhbye now
      return true;
    }
    case P4Atom::EMPTY_TYPE: return true;
    case P4Atom::INACCESSIBLE_TYPE: return false;

    case P4Atom::START_TYPE: {
      //DP.printf("IAMI P4Atom::START_TYPE@0x%x\n",(u32) &ca);
      if (hb.mCommonArgs[1] != U16_MAX)
        ca = P4Atom::makeAtom((u16) hb.mCommonArgs[1]);
      else 
        ca = P4Atom::makeAtom(PHY_DREG);
      return true;
    }

    case PHY_FB1: {
      u32 ngbsn = between(1u,4u);
      ew.mAtoms[ngbsn] = ca;
      return true;
    }

    case PHY_FB4: {
      ca.mStg[1]++;             // cheat and access underlying u32
      //ca.mStg[1] ^= 1u;             // cheat and access underlying u32
      for (u32 ngbsn = 1u; ngbsn <= 40u; ++ngbsn) { //MAX FB!
        P4Atom & na = ew.mAtoms[ngbsn];
        na = ca; // kaboom
      }
      return true;
    }

    case PHY_DREG: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      u32 natype = na.getType();
      if (natype == P4Atom::EMPTY_TYPE) {
        if (oneIn(250u)) na = ca; // New me!
        else if (oneIn(25u)) na = P4Atom::makeAtom(PHY_RES);
        else ew.swap(0u,ngbsn);
      } else if (natype == PHY_DREG) { // dreg on dreg battle
        if (oneIn(20u)) na = P4Atom::makeEmptyAtom();
      } else if (oneIn(40u)) { // general destruction
        na = P4Atom::makeEmptyAtom();
        if (!ew.swap(0u,ngbsn))
          FAIL(USER_REQUESTED_FAILURE); // XXX use swap retval
      } 
      return true;
    }
      
    case PHY_RES: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      if (na.getType() == P4Atom::EMPTY_TYPE)
        ew.swap(0u,ngbsn);
      return true;
    }
    }
    //// END OF "PHYSICS" ////
    
    return false; // NOT REACHED
  }

  //  T6Grid theT6Grid;

  bool processHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    if (theEwpL1Data.isUninitted(ngbidx)) return false;

    if (!theEwpL1Data.isActive(ngbidx)) {
      HBPTAG(hPROCBLOC,ngbidx);
      //HBPVAL(theEwpL1Data.getPublicEPState(ngbidx));
      return false;             // wait a bit
    }

    using EwpData = T6EPL1Data<EwpBlockStg,8>;
    EwpData::CarIdxs & idxs = theEwpL1Data.mTheCarIdxs[ngbidx];
    EwpData::CarIdxRB & crbi = idxs.mTheIdxs[EwpData::CarIdxs::COMM2COMP];
    EwpData::CarIdxRB & crbo = idxs.mTheIdxs[EwpData::CarIdxs::COMP2COMM];
    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    HBNOTE("hub/prcHC|0");

    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[ngbidx];
    HBASSERT_LS(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    EwpPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

    HBNOTE("hub/stconly");
    car.closeTC(sizeof(pay)); // ready to go
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    HBPTAG(hub/AFTCLOS,&crbo);
    
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

    HBPTAG(PROCINTERHUB,ngbidx);
    HBPTAG(pIHS,inside);

    using IHubData = T6EPL1Data<InterHubStorage,4>;
    IHubData::CarIdxs & idxs = theInterHubL1Data.mTheCarIdxs[ngbidx];
    IHubData::CarIdxRB & crbi = idxs.mTheIdxs[IHubData::CarIdxs::COMM2COMP];
    IHubData::CarIdxRB & crbo = idxs.mTheIdxs[IHubData::CarIdxs::COMP2COMM];
    HBPTAG(PRINHU-crbi,&crbi);

    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    HBNOTE("ihub/prcHC");
    HBPVAL(ngbidx);
    HBPVAL(carindex);

    InterHubStorage & cars = theInterHubL1Data.mTheTCStorages[ngbidx];
    HBASSERT_LS(carindex, cars.getCarCount());
    InterHubBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    InterHubPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

    HBNOTE("ihub/stconly");
    car.closeTC(sizeof(pay)); // ready to go
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    HBPTAG(ihub/AFTCLOS,&crbo);
    
    return true;
  }

  int initB() {
    HBPTAG(iNitB,fAll.mNoC0);
    preloadT2Mailbox();
    return 0;
  }

  int liveB(HostBlock & hb) {
    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    //hb.addBytes('L',hartChar(fAll.mHartNum));
    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    HBMARK;

    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xffff) == 0) {
        HBXVAL(spin);
        hb.hartbeat(fAll.mHartNum);
      }
      bool work = false;
      HBPTAG(clams,spin);
      for (u32 i = 0u; i < 4u; ++i) {
        //HBPTAG(FORCHA,i);
        //SNAP(10,HBPTAG(fIHC,i));
        if (processInterHubCars(i,hb,(i&1)==0)) {
          HBPTAG(dIHC,i);
          work = true;
        }
      }

      HBPTAG(bombs,spin);
      for (u32 e = 0u; e < 8u; ++e) {
        if (processHubCars(e,hb,true)) {
          HBPTAG(dpHC,e);
          work = true;
        }
      }

      if (!work)
        breathe();
    }
    return 0;
  }
}
