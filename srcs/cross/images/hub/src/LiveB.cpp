#include "LiveB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TransportBlock.h"
#include "EventWindow.h"
#include "T6Grid.h"
#include "NRIUtils.h" // for NRI3

#include "AllImageBlockDecls.h"
#include "ImageConfig.h"        // for theImageBlock

#include "T6STVL.h" // XXX TESTING

namespace MFM {

  extern int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  T6Grid theT6Grid[1] __attribute__ ((section(".crossrodata")));

  struct FastB {
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
  };
  FAST_LOCAL(FastB,fB,b);

  typedef T6STVL<0,0,T6GRID_WIDTH,T6GRID_HEIGHT, 4, 1'000'000> T6EWLocker;
  T6EWLocker theT6EWLocker;

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

  EWCarStorage theEWHub[8];

  int liveB(HostBlock & hb) {
    {
      P4Atom & a = theT6Grid[0].getAtom({1,1});
      a = P4Atom::makeAtom(P4Atom::START_TYPE);
      DP.printf("(%d,%d) HUBSZ6G(%ux%u)->%u, %04x:%04x-%08x-%08x\n",
                hb.mPos.x,hb.mPos.y,
                T6GRID_WIDTH, T6GRID_HEIGHT, sizeof(theT6Grid),
                a.mParityAndType,a.mData0,
                a.mStg[0],a.mStg[1]
                );
    }
    
    DP.printf("(%d,%d) EWHUBSZ(%u) of %u\n",
              hb.mPos.x, hb.mPos.y,
              sizeof(theEWHub), sizeof(EWCarStorage));
    //    XXX_DEBUG_FUNC(__FILE__,__LINE__);
    preloadT2Mailbox();
    //    XXX_DEBUG_FUNC(__FILE__,__LINE__);

 if (true) { // TEST BLOCKING L1 READS
      DP.printf("TESTBL1R (%u,%u)\n",hb.mPos.x,hb.mPos.y);

      U8C usnoc = hb.mPos;
      U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
      //DP.printf("PREBLIRD! (%u,%u)\n", usct6.x, usct6.y);
      if (!U8C::onBoardCT6Coord(usct6))
        DP.printf("FUCKAGE (%u,%u)->%u,%u\n",
                  usnoc.x, usnoc.y, usct6.x, usct6.y);
      else {
        U8C us2 = usct6;
        U8C themct6 = usct6 + S8C(1,0); // look east young man
        if (!U8C::onBoardCT6Coord(themct6))
          DP.printf("OFFBOARD (%u,%u)->%u,%u\n",
                    usct6.x, usct6.y, themct6.x, themct6.y);
        else {
          NRI3 nri3;
          u32 themibh[5];
          {
            u32 * ibu = (u32*) &theImageBlock;
            DP.printf("OURIB 0x%x:0x%08x 0x%x:0x%08x 0x%x:0x%08x\n",
                      &ibu[0],ibu[0],
                      &ibu[1],ibu[1],
                      &ibu[2],ibu[2]);
          }
          ImageBlockHeader tibh;
          bool ret = nri3.blockingL1Read(usct6, themct6,
                                         (u32) &theImageBlock,
                                         sizeof(tibh)>>2u, (u32*) &tibh);

          DP.printf("BLIRD! %d (%u,%u)<-(%u,%u)==0x%08x\n",
                    ret, us2.x, us2.y, themct6.x, themct6.y, *(u32*) &tibh);
          if (ret) {

            for (u32 e = 0u; e < tibh.mEntries; ++e) {
              
            }
          }
        }
          
      }
    }

    P2PEWElevatorPlatform & ewp = theT6ElevatorTransport.mP2PEWTransport;
    typedef P2PEWElevatorPlatform::EWCar EWCar;
    const u32 LCR = 1'000'000u;

    XXX_DEBUG_FUNC(__FILE__,__LINE__);

    u32 spin = 0u;
    if (false) {
    // XXX TEST EWLOCKER
    T6EWLocker::Entry lentry;
    bool b = theT6EWLocker.tryLock(U8C(20,10),lentry);
    DP.printf("STVL %d (%u,%u) 0x%08x %c\n",
              b,
              lentry.mPosition.x,
              lentry.mPosition.y,
              lentry.mWhenAllocated,
              '.');

    }
    while (true) {
      if ((++spin & 0x1fffff) == 0) {
        hb.hartbeat(fAll.mHartNum);
        if (false) DP.printf("hubhBRND IoH %d CrBu %08x > %02x (atm%d)\n",
                  fAll.mInspirationOnHand,
                  fAll.mCreativityBuffer,
                  createBits(8),
                  fB.mEWsAttempted);
      }
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
            if (once < 2) {
              DP.printf("(%u,%u)EWSUC! %d  %d/%d/%d #%d\n",
                        fAll.mPos.x, fAll.mPos.y,
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

  

}
