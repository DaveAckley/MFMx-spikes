#include "LiveB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TransportBlock.h"
#include "EventWindow.h"
#include "T6Grid.h"
#include "T6CellO.h"
#include "NRIUtils.h"
#include "E2HEP.h"

namespace MFM {

  extern int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  EWCarStorage theEWPCarStorage;// __attribute__ ((section(".transportblockew")));

  struct FastB {
    T6CellO mCello;
    EventWindow mFastEW;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
    BaseCarMetadata mEWP2HUBMeta[EWCarStorage::CAR_COUNT];
  };
  FAST_LOCAL(FastB,fB,b);

  E2HEP theE2HEl;

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
    if (sizeof(theE2HEl) > 3)
      DP.printf("E2HEP at %p\n",&theE2HEl);
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
    
    DP.printf("CHUB10 h6=%u,%u h0=%u,%u\n",
              hubct6.x,hubct6.y,hubnoc.x,hubnoc.y);

    S8C offct6 = S8C(hubct6) - ourct6;
    u32 index = cello.mImageTypeIndex;                // which of our kind are we?
    DP.printf("CHUBA u0=%u,%u hc=%u,%u h0=%u,%u uc=%u,%u \n",
              ournoc.x,ournoc.y,
              hubc.x,hubc.y,
              hubnoc.x, hubnoc.y,
              cello.mCellPosCT6.x,cello.mCellPosCT6.y);
    T6NgbL1Block l1b;
    if (!l1b.init(ournoc, offct6, BlockCode::BC_EWHUB))
      DP.printf("L1B INVAL\n");
    {
      u32 addr = l1b.mBaseAddress;
      DP.printf("v%d HUBBA al=%u sz=%uB addr=0x%x (%u,%u)->(%u,%u)\n",
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

  void initForHubEWPs(HostBlock & hb, T6CellO & cello) {
    DP.printf("IHUE10\n");
    theE2HEl.initCars(&theE2HCarStorage.mEWCars[0],
                      &fB.mEWP2HUBMeta[0],
                      EWCarStorage::CAR_COUNT,
                      computeHUBAddressForCellIndex(hb,cello),
                      false);
    //cello.to_repr(DP);
    theE2HEl.to_repr(DP);
  }
  void processHubEWPs() {
    theE2HEl.update();
  }

  typedef P2PEWElevatorPlatform::EWCar EWCar;

  bool processEWCar(HostBlock & hb, P2PEWElevatorPlatform & ewp) {
    EWCar * ewc = ewp.getCurrentCarIfAny();
    if (ewc) {
      if (ewc->getCarState() != CarState::OPEN) {
        ewp.advanceToNextCar();
        return true;
      }
      if (fB.mEWsAttempted%1000u == 0u) {
        DP.printf("ewphBRND IoH %d CrBu %08x > %02x\n",
                  fAll.mInspirationOnHand,
                  fAll.mCreativityBuffer,
                  createBits(8));
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
        C9printf("(%u,%u)EWSUC! %d/%d/%d #%d\n",
                 fAll.mPos.x, fAll.mPos.y,
                 fB.mEWsAttempted,
                 fB.mEWsSucceeded,
                 fB.mEWsFailed,
                 ewp.getCurrentCarIndex());
        memcpy(&ewb.mNew,&fB.mFastEW,sizeof(EventWindow));
        {
          AtomicScopeLock guard(ewp.getPlatformLock()); 
          ewc->setCarState(CarState::CLOSED, CarType::STANDARD); // let god sort it out
          C9printf("EWCLOSD #%d\n",
                   ewp.getCurrentCarIndex());
        }
      }
    }
    return true;
  }

  int liveB(HostBlock & hb) {
    preloadT2Mailbox();
    DP.printf("ELIB10\n");

    if (!fB.mCello.init())
      FAIL(ILLEGAL_STATE);

    DP.printf("LIB11\n");

    U8C hubct6 = fB.mCello.getCellPofImage(ImageCode::IC_HUB); // intra-cell pos
    U8C hubnoc = fB.mCello.getNoC0ofCellP(hubct6);             // full NoC0 of our hub
    bool doHUB = !hubnoc.isMaxed();
    if (doHUB) { // If our cell actually has a hub..
      u32 ourewpindex = fB.mCello.mImageTypeIndex;

      DP.printf("LIB12 %u,%u\n",hubnoc.x,hubnoc.y);
      initForHubEWPs(hb,fB.mCello);
      DP.printf("LIB13 %u,%u\n",hubct6.x,hubct6.y);
      {

        //// TRY TO ACCESS HUB'S EWHUB
        if (U8C::onBoardNoC0Coord(hubnoc)) { // if hub actually exists
          T6Neighbor hubngb;
          DP.printf("S8EW h6=%u,%u h0=%u,%u\n",
                    hubct6.x,hubct6.y,
                    hubnoc.x,hubnoc.y);
          S8C tohub = S8C(hubct6) - fB.mCello.mUsCT6; //from ewp,us to hub,them
          hubngb.init(hb.mPos,tohub);
          ImageBlockAddr iba = hubngb.findIBAIfAny(BlockCode::BC_EWHUB);
          if (iba.isValid()) {
            if (ourewpindex >= iba.getArrayLength()) FAIL(OUT_OF_RESOURCES);
            u32 ourhubaddr = iba.getBlockAddr() + ourewpindex*sizeof(EWCarStorage);
            DP.printf("EWHUUB %d bc=%u ba=0x%x hco=%u al=%d IX=%u AD=0x%x\n",
                      iba.isValid(), iba.getBlockCode(), iba.getBlockAddr(),
                      iba.getHostChunkOffsetOpt(), iba.getArrayLength(),
                      ourewpindex, ourhubaddr);
          }
        } else DP.printf("[%u,%u] T6NGBINFO HUBOFF\n", hb.mPos.x,hb.mPos.y);
      }
    }

    P2PEWElevatorPlatform & ewp = theT6ElevatorTransport.mP2PEWTransport;
    const u32 LCR = 1'000'000u;

    u8 spin = 0u;

    while (true) {
      if (++spin == 0) {
        hb.hartbeat(fAll.mHartNum);
        XXX_DEBUG_FUNC(__FILE__,__LINE__);
      }
      if (processEWCar(hb,ewp)) continue;
      if (doHUB) processHubEWPs();
      return 0;
    }
  }
  

}
