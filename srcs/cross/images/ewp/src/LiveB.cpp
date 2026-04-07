#include "DefaultLives.h" // for liveB
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "EP.h"
#include "TC.h"
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"

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

  bool processEwpCars(HostBlock & hb,bool inside) {

    extern EwpEP::CarIdxs theEwpBlockIdxs[1];
    EwpEP::CarIdxRB & crb = theEwpBlockIdxs[0].mIdxs[EwpEP::TC2EP];
    memoryFence();

    u8 carindex;
    if (!crb.remove(carindex)) return false;
    if (true) hb.addBytes('|','0'+carindex);

    EwpBlockStg & cars = theEwpBlockCars[0];
    MFM_API_ASSERT(carindex < cars.getCarCount(),ARRAY_INDEX_OUT_OF_BOUNDS);
    EwpBlock & car = cars.getTC(carindex);
    MFM_API_ASSERT(car.getTCState() == TCCommon::TCState::OPEN,ILLEGAL_STATE); 
    EwpPayload & pay = car.payload();
    pay.update(inside); // kilroy was here

#ifndef BUILD_HOST      
    if (true) {
      static u32 once = 0;
      if (++once < 100) {
        extern HostBlock theHostBlock;
        char buf[100];
        npf_snprintf(buf,100,"[%u] i%u stconly %s sz%u\n",
                     __LINE__,inside,
                     car.getName(),
                     sizeof(pay));
        theHostBlock.packString(buf);
      }
    }
#endif
    car.setTCState(TCCommon::TCState::CLOSED,sizeof(pay)); // ready to go

    return true;
  }

  int liveB(HostBlock & hb) {
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
      if (!processEwpCars(hb,true) && !processEwpCars(hb,false))
        breathe();
      
    }
    return 0;
  }
}
