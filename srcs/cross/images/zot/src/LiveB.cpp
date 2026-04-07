#include "DefaultLives.h" // for liveB
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "ZotBlock.h"

#include "demo.h" // for demo stuff

namespace MFM {

  //T6EPL1Data<ZotBlockStg,ZOTBLOCKS_DEMO_COUNT> theZotBlockL1Data; << in ZotBlock.cpp

  typedef PtrPair<L1Stuff,FastStuff> DemoPtrPair;

#if 0
  template<>
  DemoPtrPair myDemo<L1Stuff,FastStuff,LdSectionName<'b'>>() {
    return getPtrPair() ;
  }
#endif

#if 0
  struct FastB {
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
  };
  FAST_LOCAL(FastB,fB,b);
#endif

  /// ZOT SPECIFIC -- moved to ZotBlock.cpp
  //ZotEP mMyZotEPIN;
  //ZotEP mMyZotEPOUT;

    

  //typedef T6STVL<0,0,T6GRID_WIDTH,T6GRID_HEIGHT, 4, 1'000'000> T6EWLocker;
  //T6EWLocker theT6EWLocker;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

#if 0
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
#endif

  //  T6Grid theT6Grid;

  bool processZotCars(HostBlock & hb,bool inside) {
    const u32 DIR_IDX = inside ? ZOTBLOCKS_IN_IDX : ZOTBLOCKS_OUT_IDX;

    if (!theZotBlockL1Data.isActive(DIR_IDX)) {
      HBNOTE("zPROCBLOC");
      SNAP(5,HBPVAL(theZotBlockL1Data.getPublicEPState(DIR_IDX)));
      return false;             // wait a bit
    }

    using ZotData = T6EPL1Data<ZotBlockStg,ZOTBLOCKS_DEMO_COUNT>;

    ZotData::CarIdxs & idxs = theZotBlockL1Data.mTheCarIdxs[DIR_IDX];
    ZotData::CarIdxRB & crbi = idxs.mTheIdxs[ZotData::CarIdxs::COMM2COMP];
    ZotData::CarIdxRB & crbo = idxs.mTheIdxs[ZotData::CarIdxs::COMP2COMM];
    memoryFence();

    u8 carindex;
    if (!crbi.remove(carindex)) return false;
    HBNOTE("zot got|");
    HBPVAL(inside);

    ZotBlockStg & cars = theZotBlockL1Data.mTheTCBlocks[DIR_IDX];
    HBASSERT_LS(carindex, cars.getCarCount());
    ZotBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(),TCState::OPEN); 
    ZotPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

    HBNOTE("zot/PRECLOS");
    car.closeTC(sizeof(pay)); // every car is a full car
    MFM_API_ASSERT(!crbo.isFull(),ILLEGAL_STATE);
    crbo.add(carindex);
    HBNOTE("zot/AFTCLOS");

    return true;
  }

  int liveB(HostBlock & hb) {
    auto ptpr = getPtrPair<L1Stuff,FastStuff,LdSectionName<'b'>>();
    HBNOTE("AKAPP");
    HBPVAL(ptpr.mFastPriv);
    HBPVAL(ptpr.mL1Pub);
    
    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    //hb.addBytes('L',hartChar(fAll.mHartNum));
    preloadT2Mailbox();
    //hb.addBytes('B',hartChar(fAll.mHartNum));

    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop

    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xfff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      if (!processZotCars(hb,true) && !processZotCars(hb,false))
        breathe();
      
    }
    return 0;
  }
}
