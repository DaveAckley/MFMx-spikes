#include "EP_InterHub.h"
#include "EwpBlock.h" // for theEwpL1Data
#include "FastT2.h" // for oneIn
#include "HubLiveB.h" // for fB
#include "T6PhaseLockedLoop.h" // for thePLL
#include "CacheLoader.h" // for CacheLoader eg

namespace MFM {
  T6EPL1Data<InterHubStorage,8> theInterHubL1Data;
  T6EPL1Data<InterHubStorage,4> theCornerHubL1Data;

  //////// LIVEB STEP TASK?
  //FAST_LOCAL(InterHubPrivateControl,myPIHControlHB,b); >> moved to LiveB/fB

  void InterHubPrivateControl::init() {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    memset_s(this,'\0',sizeof(*this)); // init all
    //mPrivateState = IHH_PAUSE;
    //    HBPTAG(PIHinit,getIHHStateName(mPrivateState));
  }

#if 0
  void InterHubPrivateControl::stepB(HostBlock &hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    L1GridManagerControl & lgmc = theL1GridManagerControl;

    // "processInterHubCars"
    for (u32 i = 0u; i < 8u; ++i) {
      updateIHCarsB(i,hb,(i&1)==0);
    }
  }

  /* returns true iff done, false if should go again */
  bool InterHubPrivateControl::_ucLeaderLOAD_CACHE_B(u32 ngbidx, 
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    EACH(1'000,LOGPTAG(_ducLLCBE,__EACHNUM__));
    CacheLoaders & cls = theL1CacheLoaders;
    CacheLoader & cl = cls.get((Dir8) ngbidx);

    if (!cl.needsAnotherPacket()) return true;
    if (crbi.isEmpty() || crbo.isFull()) return false;

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// LOAD_CACHE    leader    expect full cars,
    ////                         stash atoms in cache,
    ////                         ship emptied or empties

    u8 carindex;
    crbi.remove(carindex);

#if 0
    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[0];
    HBASSERT_LT(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    EACH(10'000,HBPTAG(ewp/INSIZ,car.currentTCSize()));

    EwpPayload & pay = car.payload();
    bool ret = fB.mThePhysics.doTransition(pay);
#endif    
    
  }

  /* returns true iff done, false if should go again */
  bool InterHubPrivateControl::_ucFollowerLOAD_CACHE_B(u32 ngbidx,
                                                       GridManager & gm,
                                                       TheL1Data::CarIdxRB & crbi,
                                                       TheL1Data::CarIdxRB & crbo)
  {
    EACH(1'000,LOGPTAG(_ducFLCB,__EACHNUM__));

    CacheLoaders & cls = theL1CacheLoaders;
    CacheLoader & cl = cls.get((Dir8) ngbidx);

    if (!cl.needsAnotherPacket()) return true;
    if (crbi.isEmpty() || crbo.isFull()) return false;

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// LOAD_CACHE    follower  expect empty cars,
    ////                         load&ship from sites

    LOGPTAG(_ducFLCBngb,dir8ToByteString((Dir8) ngbidx));
    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[ngbidx];

    u8 carindex;
    crbi.remove(carindex);
    LOGPTAG(_ducFLCBcix,carindex);

    InterHubBlock & ihb = ihs.getTC(carindex);
    
    cl.loadPacket(gm, ihb);
    LOGPTAG(_ducPKTdone,carindex);               // ship damofo gogogo
    crbo.add(carindex);
    return false;               // and let me take another look
  }

  /* returns true iff done, false if should go again */
  bool InterHubPrivateControl::_ucFollowerRUN_EVENTS_B(u32 ngbidx,
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    EACH(1'000'000,LOGPTAG(:,__FUNCTION__));
    FAIL(INCOMPLETE_CODE);
  }
  
  /* returns true iff done, false if should go again */
  bool InterHubPrivateControl::_ucLeaderSAVE_CACHE_B(u32 ngbidx,
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    EACH(1'000'000,LOGPTAG(:,__FUNCTION__));
    FAIL(INCOMPLETE_CODE);
  }

  /* returns true iff done, false if should go again */
  bool InterHubPrivateControl::_ucFollowerSAVE_CACHE_B(u32 ngbidx,
                                                       GridManager & gm,
                                                       TheL1Data::CarIdxRB & crbi,
                                                       TheL1Data::CarIdxRB & crbo)
  {
    EACH(1'000'000,LOGPTAG(:,__FUNCTION__));
    FAIL(INCOMPLETE_CODE);
  }

  void InterHubPrivateControl::updateIHCarsB(u32 ngbidx,HostBlock & hb,bool inside) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    MFM_API_ASSERT(ngbidx<IHUB_BLOCKS,ILLEGAL_ARGUMENT);

    GridManager & gm = fB.mGridManager;
    SCStatus whatWeDo = gm.leadFollowOrGetOutOfTheWay();

    bool iLead = whatWeDo == SCStatus::WE_LEAD;
    bool iFollow = whatWeDo == SCStatus::WE_FOLLOW;

    TheL1Data::CarIdxRB & crbi = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    // All other cases want to move a car
    if (crbi.isEmpty() || crbo.isFull()) {
      const u32 EACHCOUNT = 1'000'000;
      EACH(EACHCOUNT,{
          LOGPTAG(BLOKT, // 0xNN0ee0ff
                  ((__EACHNUM__/EACHCOUNT)<<24)+
                  ((((u32)crbi.isEmpty())*0xee)<<12)+
                  ((((u32)crbi.isFull())*0xff)<<0));
        });
      return;
    }

    EACH(1'000'000,{
        LOGPTAG(_ducDO,getSCStatusName(whatWeDo));
        LOGPTAG(_ducEA,__EACHNUM__);
      });

    if (thePLL.ifCouldAck(PBCmd::RUN_EVENTS,Edge::ENTERING)) {
      if ((iLead && _ucLeaderLOAD_CACHE_B(ngbidx, gm, crbi, crbo)) ||
          (iFollow && _ucFollowerLOAD_CACHE_B(ngbidx, gm, crbi, crbo))) {
        thePLL.ackEntering(PBCmd::RUN_EVENTS);
        HBPTAG(VOTE!,thePLL.voteStatus());
      }
    }

    if (thePLL.ifCouldAck(PBCmd::NEW_LEADER,Edge::ENTERING)) { 
      if ((iLead && _ucLeaderSAVE_CACHE_B(ngbidx, gm, crbi, crbo)) ||
          (iFollow && _ucFollowerSAVE_CACHE_B(ngbidx, gm, crbi, crbo))) {
        thePLL.ackEntering(PBCmd::NEW_LEADER);
        HBPTAG(VOTE!,thePLL.voteStatus());
      }
    }
  }
#endif
  
  void InterHubEP::initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    //    HBPTAG(IHPT,getNameFromIHPType(IHPT_PING));
    //    HBPX(sizeof(IHPPing));
    //    HBPX(sizeof(IHPPing::HopReport));
    //HBPX(sizeof(IHPAtoms));
    //HBPX(sizeof(InterHubPayload));
    //    HBPX(sizeof(InterHubBlock));
    HBPX(sizeof(InterHubStorage));
    this->initT6EP(srcEPA, isin, l1data);
  }

  bool InterHubEP::shipTC(SUBTC & car, u8 carindex) {
    ASSERT_RIGHT_HART();
    Buf32 b32;

    /* New plan: Intervene at configureDest so that mDestNoC0 and
       mDestBlockAddr are already modified for the diagonal shots, and
       there's no diversion here.

       (Old) Plan: Intervene if this IH car needs a bankshot to reach
       our NE or SW corner. Then dispatch to shipTCTo in any case.
    */

    //LOGPTAG(STC,car.formatTC(*this));
    if (false) {
      const EndPointAddress sEPA = getSrcEPA();
      u8 d8 = sEPA.mBlockCodeIndex;
      MFM_API_ASSERT(d8 <= D8_NE, ILLEGAL_STATE);
      const char * n = dir8ToByteString((Dir8) d8);
      EACH(1,{ if (d8&1) LOGPTAG(IHEPsTCDIVERT,n); else LOGPTAG(IHEPsTC,n); });
    }
    u64 destcaraddr = mDestBlockAddr + CAR_SIZE*carindex;
    U8C ournoc0 = fAll.mNoC0;
    U8C destnoc0 = mDestNoC0;
    LOGPTAG(.ps, formatT6SendBuf32(b32,&car,car.currentTCSize(),
                                    car.getHeader().getPacketNonce(),
                                    destnoc0,(void*) (u32) destcaraddr));
    return shipTCTo(car,ournoc0,destnoc0,destcaraddr);
  }

  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    Buf32 b32;

    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??
    LOGPTAG(.pr,formatT6RecvBuf32(b32,&car,car.currentTCSize(),
                                   car.getHeader().getPacketNonce(),
                                   (Dir8) getSrcEPA().mBlockCodeIndex));
    // Open it up
    car.openTC();
    crbi.add(carindex); //notify hB

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

}
