#include "LiveB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TC.h"
#include "EventWindow.h"
#include "T6Grid.h"
#include "T6CellO.h"
#include "NRIUtils.h"
#include "E2HEP.h"

//#define P DP
#define P LOG

namespace MFM {

  extern int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  EWCarStorage theEWPCarStorage;// __attribute__ ((section(".transportblockew")));

  struct FastB {
    T6CellO mCellO;
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
    CarOpsTimers mEWP2HUBCarOpsTimers[EWCarStorage::CAR_COUNT];
  };
  FAST_LOCAL(FastB,fB,b);

  E2HEP theE2HEP;

  EWCarStorage theE2HCarStorage;

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
      u32 age = ++ca.mStg[1];             // cheat and access underlying u32
      u32 empcount = 0u;
      for (u32 ngbsn = 1u; ngbsn <= 40u; ++ngbsn) { //MAX FB!
        P4Atom & na = ew.mAtoms[ngbsn];
        bool isempty = na.getType()==P4Atom::EMPTY_TYPE;
        if (isempty) ++empcount;
        u32 div = (isempty ? 1024u : 64u);
        if (create(age/div)==0) { // if youth tends to displace age, can we see a difference?
          na = ca; // kaboom
        }
      }
      if (empcount == 40u && age > 1024u) // I'M OLD AND ALL ALONE? WTF?
        ca.mStg[1] = 0u;         // MIRACLE CURE FOUNTAIN OF YEWT
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

  u32 findHubEWAddress(HostBlock & hb, T6CellO & cello) {
    if (sizeof(theE2HEP) > 3)
      P.printf("E2HEP at %p\n",&theE2HEP);
    return 0u;
    /*
    */
  }

  u32 computeHUBAddressForCellIndex(HostBlock& hb, T6CellO & cello) {
    U8C hubc = cello.getCellPofImage(ImageCode::IC_HUB); // intra-cell pos of hub
    U8C hubnoc = cello.getNoC0ofCellP(hubc);             // full NoC0 of our hub
    U8C ournoc = cello.getNoC0ofUs();
    U8C ourct6 = cello.getUsCT6();
    U8C hubct6 = U8C::makeCT6CoordFromNoC0Coord(hubnoc);
    
    P.printf("CHUB10 h6=%u,%u h0=%u,%u\n",
              hubct6.x,hubct6.y,hubnoc.x,hubnoc.y);

    S8C offct6 = S8C(hubct6) - ourct6;
    u32 index = cello.mImageTypeIndex;                // which of our kind are we?
    P.printf("CHUBA u0=%u,%u hc=%u,%u h0=%u,%u uc=%u,%u \n",
              ournoc.x,ournoc.y,
              hubc.x,hubc.y,
              hubnoc.x, hubnoc.y,
              cello.mCellPosCT6.x,cello.mCellPosCT6.y);
    T6NgbL1Block l1b;
    if (!l1b.init(ournoc, offct6, BlockCode::BC_EWHUB))
      P.printf("L1B INVAL\n");
    {
      u32 addr = l1b.mBaseAddress;
      P.printf("v%d HUBBA al=%u sz=%uB addr=0x%x (%u,%u)->(%u,%u)\n",
                l1b.isValid(),
                l1b.mArrayLength,
                l1b.mItemSize,
                addr,
                ourct6.x, ourct6.y,
                hubct6.x, hubct6.y);
      return addr;
    }
    return 0;
  }

  void initForHubEWPs(HostBlock & hb, T6CellO & cello, U8C hubnoc0, u32 ewhubaddr) {
    //P.printf("IHUE10\n");
    u32 ourewpindex = cello.mImageTypeIndex;
    theE2HEP.initCars(ourewpindex,
                      hb.mPos,
                      &theE2HCarStorage.mEWCars[0],
                      &fB.mEWP2HUBCarOpsTimers[0],
                      hubnoc0,
                      ewhubaddr);
    //cello.to_repr(DP);
    //theE2HEP.to_repr(DP);
  }
  void processHubEWPs(HostBlock & hb) {
    static u32 spin = 0u;
    if ((spin++ % 100'000'000u) == 0)
      P.printf("pHEWPs %u [%u,%u]\n",spin,hb.mPos.x,hb.mPos.y);
    theE2HEP.update();
  }

  //typedef P2PEWElevatorPlatform::EWCar EWCar;

  bool processEWCar(HostBlock & hb, P2PEWElevatorPlatform & ewp) {
    EWCar * ewc = ewp.getCurrentCarIfAny();
    if (ewc) {
      if (ewc->getCarState() != CarState::OPEN) {
        ewp.advanceToNextCar();
        return true;
      }
      if (fB.mEWsAttempted%50000u == 0u) {
        P.printf("%s:EWs %d (+ %d, - %d) #%d ioh%u:0x%08x\n",hartName(fAll.mHartNum),
                  fB.mEWsAttempted,
                  fB.mEWsSucceeded,
                  fB.mEWsFailed,
                  ewp.getCurrentCarIndex(),
                  fAll.mInspirationOnHand,
                  fAll.mCreativityBuffer);
      }
      EWBlock & ewb = ewc->getContent();
      ++fB.mEWsAttempted;
      memcpy(&fB.mFastEW,&ewb.mOld,sizeof(EventWindow));
      if (!updateFastEW(hb)) ++fB.mEWsFailed;
      else {
        ++fB.mEWsSucceeded;
        if (false) C9printf("(%u,%u)EWSUC! %d/%d/%d #%d\n",
                 fAll.mPos.x, fAll.mPos.y,
                 fB.mEWsAttempted,
                 fB.mEWsSucceeded,
                 fB.mEWsFailed,
                 ewp.getCurrentCarIndex());
        memcpy(&ewb.mNew,&fB.mFastEW,sizeof(EventWindow));
        {
          AtomicScopeLock guard(ewp.getPlatformLock()); 
          ewc->setCarState(CarState::CLOSED, CarType::STANDARD); // let god sort it out
          if (false) C9printf("EWCLOSD #%d\n",
                   ewp.getCurrentCarIndex());
        }
      }
    }
    return true;
  }

  int liveB(HostBlock & hb) {
    preloadT2Mailbox();
    P.printf("ELIB10\n");

    if (!fB.mCellO.init())
      FAIL(ILLEGAL_STATE);


    U8C hubct6 = fB.mCellO.getCellPofImage(ImageCode::IC_HUB); // intra-cell pos
    U8C hubnoc = fB.mCellO.getNoC0ofCellP(hubct6);             // full NoC0 of our hub
    bool doHUB = !hubnoc.isMaxed();
    P.printf("LIB11 doHUB%d\n",doHUB);
    if (doHUB) {
      T6Neighbor hubngb;
      S8C tohub = S8C(hubct6) - fB.mCellO.mUsCT6; //from ewp,us to hub,them
      hubngb.init(hb.mPos,tohub);
      ImageBlockAddr iba = hubngb.findIBAIfAny(BlockCode::BC_EWHUB);
      if (iba.isValid()) {
        initForHubEWPs(hb,fB.mCellO,hubnoc, iba.getBlockAddr());
      } else doHUB = false;
    }
    //P.printf("LIB13 %u,%u\n",hubct6.x,hubct6.y);

#if 0
    if (false && doHUB) { // If our cell actually has a hub..

      P.printf("LIB13 %u,%u\n",hubnoc.x,hubnoc.y);
      if (false) {

        //// TRY TO ACCESS HUB'S EWHUB
        if (U8C::onBoardNoC0Coord(hubnoc)) { // if hub actually exists
          T6Neighbor hubngb;
          P.printf("S8EW h6=%u,%u h0=%u,%u\n",
                    hubct6.x,hubct6.y,
                    hubnoc.x,hubnoc.y);
          S8C tohub = S8C(hubct6) - fB.mCellO.mUsCT6; //from ewp,us to hub,them
          hubngb.init(hb.mPos,tohub);
          ImageBlockAddr iba = hubngb.findIBAIfAny(BlockCode::BC_EWHUB);
          if (iba.isValid()) {
            if (ourewpindex >= iba.getArrayLength()) FAIL(OUT_OF_RESOURCES);
            u32 ourhubaddr = iba.getBlockAddr() + ourewpindex*sizeof(EWCarStorage);
            P.printf("EWHUUB %d bc=%u ba=0x%x hco=%u al=%d IX=%u AD=0x%x\n",
                      iba.isValid(), iba.getBlockCode(), iba.getBlockAddr(),
                      iba.getHostChunkOffsetOpt(), iba.getArrayLength(),
                      ourewpindex, ourhubaddr);
          }
        } else P.printf("[%u,%u] T6NGBINFO HUBOFF\n", hb.mPos.x,hb.mPos.y);
      }
    }
#endif

    P2PEWElevatorPlatform & ewp = theT6ElevatorTransport.mP2PEWTransport;
    const u32 LCR = 1'000'000u;

    u8 spin = 0u;

    while (true) {
      if (++spin == 0) {
        hb.hartbeat(fAll.mHartNum);
        XXX_DEBUG_FUNC(__FILE__,__LINE__);
      }
      if (doHUB) processHubEWPs(hb);
      if (processEWCar(hb,ewp)) continue;
      return 0;
    }
  }
  

}
