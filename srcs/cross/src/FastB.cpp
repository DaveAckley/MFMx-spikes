#include "FastLocal.h"
#include "FastB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TransportBlock.h"
#include "EventWindow.h"

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
        if (oneIn(500u)) na = ca; // New me!
        else if (oneIn(50u)) na = P4Atom::makeAtom(PHY_RES);
        else ew.swap(0u,ngbsn);
      } else if (natype == PHY_DREG) { // dreg on dreg battle
        if (oneIn(10u)) na = P4Atom::makeEmptyAtom();
      } else if (oneIn(20u)) { // general destruction
        na = P4Atom::makeEmptyAtom();
        ew.swap(0u,ngbsn);
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

  static int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveB(HostBlock & hb) {
    P2PEWElevatorPlatform & ewp = theT6ElevatorTransport.mP2PEWTransport;
    typedef P2PEWElevatorPlatform::EWCar EWCar;
    const u32 LCR = 1'000'000u;

    u8 spin = 0u;
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      EWCar * ewc = ewp.getCurrentCarIfAny();
      if (ewc) {
        if (ewc->getCarState() != CarState::OPEN) {
          ewp.advanceToNextCar();
          continue;
        }
        if (fB.mEWsAttempted%1000u == 0u) {
          DP.printf("%s:EWs %d (+ %d, - %d) #%d\n",hartName(fAll.mHartNum),
                    fB.mEWsAttempted,
                    fB.mEWsSucceeded,
                    fB.mEWsFailed,
                    ewp.getCurrentCarIndex());
        }
        EWBlock & ewb = ewc->getContent();
        ++fB.mEWsAttempted;
        memcpy(&fB.mFastEW,&ewb.mOld,sizeof(EventWindow));
        if (!updateFastEW(hb)) ++fB.mEWsFailed;
        else {
          ++fB.mEWsSucceeded;
          { static u32 once;
            if (once < 1) {
              DP.printf("%d EWSUC! %d/%d/%d #%d\n",
                        once++,
                        fB.mEWsAttempted,
                        fB.mEWsSucceeded,
                        fB.mEWsFailed,
                        ewp.getCurrentCarIndex());
            }
          }
          memcpy(&ewb.mNew,&fB.mFastEW,sizeof(EventWindow));
          {
            AtomicScopeLock guard(ewp.getPlatformLock()); 
            ewc->setCarState(CarState::CLOSED, CarType::STANDARD); // let god sort it out
            { static u32 once;
              if (once < 2) {
                DP.printf("%d EWCLOSD #%d\n",
                          once++,
                          ewp.getCurrentCarIndex());
              }
            }
          }
        }
      }
    }
    return 0;
  }

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    preloadT2Mailbox();
    DP.printf("B#%d(%d,%d)\n",hb.mTLBI,hb.mXPos,hb.mYPos);
    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    // hb.mCommonArgs[1] reserved for start decay type
    // hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveB(hb);          // go do your hart t2 thing you
  }
}
