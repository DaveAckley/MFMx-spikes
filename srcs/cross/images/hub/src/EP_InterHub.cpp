#include "EP_InterHub.h"
#include "EwpBlock.h" // for theEwpL1Data
#include "FastT2.h" // for oneIn
#include "HubLiveB.h" // for fB
#include "T6BoltResponder.h" // for theT6BoltResponder
#include "CacheLoader.h" // for CacheLoader eg

namespace MFM {
  T6EPL1Data<InterHubStorage,8> theInterHubL1Data;
  T6EPL1Data<InterHubStorage,4> theCornerHubL1Data;
  InterHubL1Control theInterHubL1Control;

  //////// LIVEB STEP TASK?
  //FAST_LOCAL(InterHubPrivateControl,myPIHControlHB,b); >> moved to LiveB/fB

  void InterHubL1Control::init() {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    memset_s(this,'\0',sizeof(*this));
    mL1GridManagerControlPtr = &theL1GridManagerControl;

    /*
    for (u8 i = 0; i < IHUB_BLOCKS; ++i)
      mL1HubControls[i].init(i);
    */
  }

  L1GridManagerControl & InterHubL1Control::getL1GridManagerControl() {
    MFM_API_ASSERT_NONNULL(mL1GridManagerControlPtr);
    return *mL1GridManagerControlPtr;
  }

#if 0
  void InterHubL1Control::L1Hub1::setupNewCar(TheL1Data::CarIdxRB & crbi) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    MFM_API_ASSERT_NULL(mCurrentInterHub); // musn't already be working on a car
    if (!crbi.remove(mCurrentCarIndex))
      FAIL(ILLEGAL_ARGUMENT);

    InterHubStorage & ihs = theInterHubL1Data.mTheTCStorages[mL1HubDir8];
    HBASSERT_LT(mCurrentCarIndex, ihs.getCarCount());
    InterHubBlock & ihb = ihs.getTC(mCurrentCarIndex);
    mCurrentInterHub = &ihb;

    InterHubPayload & pay = ihb.payload();
    IHPType paytype = pay.mIHPHeader.mPayloadType;
    if (paytype != IHPT_EMPTY)
      LOGPTAG(WRONGIHPT,getNameFromIHPType(paytype));

    pay.initIHP(IHPT_ATOMS,{0,0},{0,0}); // now it's atoms either way

    LOGPTAG(L1HUB1SUNC,mCurrentInterHub);
    LOGPX(mCurrentCarIndex);
    LOGPX(mL1HubDir8);
  }
#endif

  bool InterHubL1Control::readyToClose() {
    AtomicScopeLock guard(mIHL1Lock);
    EACH(1'000,HBPTAG(HIHL1Cr2c,__EACHNUM__));
    EACH(1'000,LOGPTAG(IHL1Cr2c,__EACHNUM__));
    return false;
  }

  void InterHubPrivateControl::init(InterHubL1Control & ihl1) {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    memset_s(this,'\0',sizeof(*this)); // init all
    mIHL1Control = &ihl1;
    //mPrivateState = IHH_PAUSE;
    //    HBPTAG(PIHinit,getIHHStateName(mPrivateState));
  }

  void InterHubPrivateControl::stepB(HostBlock &hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);

    L1GridManagerControl & lgmc = theL1GridManagerControl;
    InterHubL1Control & ihl1 = getL1();
    AtomicScopeLock guard(ihl1.mIHL1Lock);

    // "processInterHubCars"
    for (u32 i = 0u; i < 8u; ++i) {
      updateCarsB(i,hb,(i&1)==0);
    }
  }

#if 0 // MOVE TO IHPC

    GridManager & gm = fB.mGridManager;
    SCStatus stat = gm.leadFollowOrGetOutOfTheWay();
    LOGPTAG(BOLTR_LOAD_CACHE,getSCStatusName(stat));
    if (stat == SCStatus::WE_LEAD || stat == SCStatus::NO_LEADER)
      return true;              // followers deal with loading cache

    bool allDone = true;
    for (u32 i = 0; i < theL1CacheLoaders.mCacheLoadersInUse; ++i) {
      CacheLoader & cl = theL1CacheLoaders.get(i);
      if (!cl.tryLoad(gm)) allDone = false;
    }
    LOGPTAG(SHID,allDone);
    return allDone;             // response complete when shipping done
#endif


  void InterHubPrivateControl::_ucLeaderLOAD_CACHE_B(u32 ngbidx,
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    MFM_API_ASSERT(!crbi.isEmpty() && !crbo.isFull(),ILLEGAL_ARGUMENT);

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// LOAD_CACHE    leader    expect full cars,
    ////                         stash atoms in cache,
    ////                         ship emptied or empties

    u8 carindex;
    crbi.remove(carindex);
    CacheLoaders & cls = theL1CacheLoaders;
    CacheLoader * clp = cls.getIfAny((Dir8) ngbidx);
    LOGPTAG(_ucLLCB,clp);    
  }

  void InterHubPrivateControl::_ucFollowerLOAD_CACHE_B(u32 ngbidx,
                                                       GridManager & gm,
                                                       TheL1Data::CarIdxRB & crbi,
                                                       TheL1Data::CarIdxRB & crbo)
  {
    MFM_API_ASSERT(!crbi.isEmpty() && !crbo.isFull(),ILLEGAL_ARGUMENT);

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// LOAD_CACHE    follower  expect empty cars,
    ////                         load&ship from sites

    LOGPTAG(_ucFLCBngb,ngbidx);
    u8 carindex;
    crbi.remove(carindex);
    LOGPTAG(_ucFLCBcix,carindex);

    CacheLoaders & cls = theL1CacheLoaders;
    CacheLoader * clp = cls.getIfAny((Dir8) ngbidx);
    MFM_API_ASSERT_NONNULL(clp);
    
    LOGPTAG(_ucFLCB,clp);
    
  }

  void InterHubPrivateControl::_ucFollowerCARRY_ON_B(u32 ngbidx,
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    EACH(100'000,LOGPTAG(:,__FUNCTION__));
  }
  

  void InterHubPrivateControl::_ucLeaderSAVE_CACHE_B(u32 ngbidx,
                                                     GridManager & gm,
                                                     TheL1Data::CarIdxRB & crbi,
                                                     TheL1Data::CarIdxRB & crbo)
  {
    EACH(100'000,LOGPTAG(:,__FUNCTION__));
  }

  void InterHubPrivateControl::_ucFollowerSAVE_CACHE_B(u32 ngbidx,
                                                       GridManager & gm,
                                                       TheL1Data::CarIdxRB & crbi,
                                                       TheL1Data::CarIdxRB & crbo)
  {
    EACH(100'000,LOGPTAG(:,__FUNCTION__));
  }
  
  void InterHubPrivateControl::updateCarsB(u32 ngbidx,HostBlock & hb,bool inside) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    MFM_API_ASSERT(ngbidx<IHUB_BLOCKS,ILLEGAL_ARGUMENT);

    //// ANYTHING TO DO?
    PhaserBolt::Cmd myCmd = theT6BoltResponder.getPrevailingCmdIfAny();
    if (myCmd == PhaserBolt::NO_CMD) return; // definitely not

    GridManager & gm = fB.mGridManager;
    SCStatus whatWeDo = gm.leadFollowOrGetOutOfTheWay();

    bool iLead = whatWeDo == SCStatus::WE_LEAD;
    bool iFollow = whatWeDo == SCStatus::WE_FOLLOW;

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// CARRY_ON      leader    let crbi ride, do big ewps
    if (iLead && myCmd == PhaserBolt::CMD_CARRY_ON) return;

    TheL1Data::CarIdxRB & crbi = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    // All other cases want to move a car
    if (crbi.isEmpty() || crbo.isFull()) {
      EACH(100'000,LOGPTAG(BLOKT,__EACHNUM__));
      return;
    }

    EACH(100'000,{
        LOGPTAG(_ucB,PhaserBolt::phaserCmdName(myCmd));
        LOGPTAG(_ucDO,getSCStatusName(whatWeDo));
      });

    if (iLead && myCmd == PhaserBolt::CMD_LOAD_CACHE)
      return _ucLeaderLOAD_CACHE_B(ngbidx, gm, crbi, crbo);

    if (iFollow && myCmd == PhaserBolt::CMD_LOAD_CACHE)
      return _ucFollowerLOAD_CACHE_B(ngbidx, gm, crbi, crbo);

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// CARRY_ON      follower  flush crbi, ship empties
    ////                         so they accumulate at the leader,
    ////                         do small ewps
    if (iFollow && myCmd == PhaserBolt::CMD_CARRY_ON)
      return _ucFollowerCARRY_ON_B(ngbidx, gm, crbi, crbo);

    //// BOLT STATE    US STATE  ACTION
    //// ==========    ========  ======
    //// SAVE_CACHE    leader    expect empty cars,
    ////                         fill&ship from caches
    if (iLead && myCmd == PhaserBolt::CMD_SAVE_CACHE)
      return _ucLeaderSAVE_CACHE_B(ngbidx, gm, crbi, crbo);

    //// SAVE_CACHE    follower  expect full cars,
    ////                         stash atoms in sites
    ////                         ship empties
    if (iFollow && myCmd == PhaserBolt::CMD_SAVE_CACHE)
      return _ucFollowerSAVE_CACHE_B(ngbidx, gm, crbi, crbo);

    if (whatWeDo != SCStatus::NO_LEADER) {
      LOGPTAG(ucmySTATUS,getSCStatusName(whatWeDo));
      LOGPTAG(ucmyCmd,PhaserBolt::phaserCmdName(myCmd));
    }
  }

  void InterHubEP::initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    //    HBPTAG(IHPT,getNameFromIHPType(IHPT_PING));
    //    HBPX(sizeof(IHPPing));
    //    HBPX(sizeof(IHPPing::HopReport));
    HBPX(sizeof(IHPAtoms));
    HBPX(sizeof(InterHubPayload));
    //    HBPX(sizeof(InterHubBlock));
    HBPX(sizeof(InterHubStorage));
    this->initT6EP(srcEPA, isin, l1data);
  }

  bool InterHubEP::shipTC(SUBTC & car, u8 carindex) {
    ASSERT_RIGHT_HART();

    /* New plan: Intervene at configureDest so that mDestNoC0 and
       mDestBlockAddr are already modified for the diagonal shots, and
       there's no diversion here.

       (Old) Plan: Intervene if this IH car needs a bankshot to reach
       our NE or SW corner. Then dispatch to shipTCTo in any case.
    */

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
    return shipTCTo(car,ournoc0,destnoc0,destcaraddr);
  }

  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    EACH(1,LOGPTAG(IHrecvTC,__EACHNUM__));

    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    crbi.add(carindex); //notify hB

    EACH(1,LOGPTAG(IHrecvTCD,carindex));

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    if (carindex >= CAR_COUNT) return 0;
    EACH(1'000'000,LOGPTAG(IHGotp,&this->getCarStg()));
    return &this->getCarStg().getTC(carindex);
  }

}
