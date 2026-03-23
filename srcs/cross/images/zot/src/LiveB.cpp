#include "LiveB.h"
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "ZotBlock.h"

namespace MFM {

  extern int liveB(HostBlock & hb) /*__attribute__ ((optimize("O2")))*/;

  struct FastB {
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
    ZotEP mMyZotEPIN;
    ZotEP mMyZotEPOUT;
  };
  FAST_LOCAL(FastB,fB,b);

  ZotBlockStg theZotBlockCarsIO[2];

  AtomicLock theZotBlockIOLock[2];

  void initZotBlocks() {
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('z',hartChar(fAll.mHartNum));
        once++;
      }
    }

    fB.mMyZotEPIN.init(true,theZotBlockCarsIO[0],theZotBlockIOLock[0]);
    fB.mMyZotEPOUT.init(false,theZotBlockCarsIO[1],theZotBlockIOLock[1]);
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('Z',hartChar(fAll.mHartNum));
        once++;
      }
    }
  }

  void updateZotBlocks() {

    fB.mMyZotEPIN.updateOps();
    fB.mMyZotEPOUT.updateOps();
    {
      static bool once = false;
      if (!once) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('!',hartChar(fAll.mHartNum));
        once = true;
      }
    }
  }

  //typedef T6STVL<0,0,T6GRID_WIDTH,T6GRID_HEIGHT, 4, 1'000'000> T6EWLocker;
  //T6EWLocker theT6EWLocker;

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

  int liveB(HostBlock & hb) {
    if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
    hb.addBytes('L',hartChar(fAll.mHartNum));
    preloadT2Mailbox();
    hb.addBytes('B',hartChar(fAll.mHartNum));

    initZotBlocks();
    hb.addBytes('C',hartChar(fAll.mHartNum));

    //P2PEWElevatorPlatform & ewp = theT6ElevatorTransport.mP2PEWTransport;
    //DP.printf("EWP @ 0x%08x\n",(u32) &ewp);
    //typedef P2PEWElevatorPlatform::EWCar EWCar;
    const u32 LCR = 1'000'000u;

    u32 spin = 0u;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    hb.addBytes('5',hartChar(fAll.mHartNum));
    while (true) {
      if (!hb.goodMagic()) FAIL(ILLEGAL_STATE);
      if ((++spin & 0xfffff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      updateZotBlocks();
    }
    return 0;
  }

  

}
