#include "EP_InterHub.h"
#include "EwpBlock.h" // for theEwpL1Data
#include "FastT2.h" // for oneIn
#include "HubLiveB.h" // for fB

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

    for (u8 i = 0; i < IHUB_BLOCKS; ++i)
      mL1HubControls[i].init(i);
  }

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

  bool InterHubL1Control::readyToClose() {
    AtomicScopeLock guard(mIHL1Lock);
    EACH(1'000,HBPTAG(HIHL1Cr2c,__EACHNUM__));
    EACH(1'000,LOGPTAG(IHL1Cr2c,__EACHNUM__));

#if 0
    if (!mCurrentACacheBlock)
      return false;             // nothing to close
    ACacheBlock & acb = *mCurrentACacheBlock;
    ACacheBlockPayload & pay = acb.payload();
    return
      pay.getBytesRemaining() == 0 || pay.checkRCloseFlag();
#endif
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
      updateCars(i,hb,(i&1)==0);
    }
  }

  void InterHubPrivateControl::updateCars(u32 ngbidx,HostBlock & hb,bool inside) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    MFM_API_ASSERT(ngbidx<IHUB_BLOCKS,ILLEGAL_ARGUMENT);

    TheL1Data::CarIdxRB & crbi = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theInterHubL1Data.getCarIdxs(ngbidx).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    MFM_API_ASSERT_NONNULL(mIHL1Control);
    InterHubL1Control::L1Hub1 & ihl1 = mIHL1Control->getL1Hub1(ngbidx);
    InterHubBlock * curihb = ihl1.mCurrentInterHub; // if any

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */

    bool keytest = false && // XXXXX FIX ME
      curihb != 0 &&          // have a car and
      !crbi.isEmpty() &&      // more empty cars are available and
      !crbo.isFull()/* &&       // more full cars are shippable and
      ihl1.readyToClose()*/;
    EACH(1000,{HBPX(keytest);HBPX(curihb);HBPX(crbi.isEmpty());HBPX(crbo.isFull());/*HBPTAG(IHBCuC,ihl1.readyToClose());*/});
    if (keytest) {  // current car is ready to go
      bool got;

      if (false) {
        LOGPX(keytest);
        LOGPX(&ihl1);

        HBPX(crbi.isEmpty());
        HBPX(crbo.isFull());
        //        HBPX(ihl1.readyToClose());
      }
      

      { // SHIPPING
        InterHubBlock & olb = *curihb;
        InterHubPayload & opay = olb.payload();
        HBPTAG(IHSHIPH,opay.getCurrentPayloadSize());
        //HBPTAG(IHSHIPR,opay.getBytesRemaining());
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        if (false) {
          if (opay.getCurrentPayloadSize() < 4)
            LOGPTAG(IHSHIPFRMS,ihl1.mCurrentCarIndex);
          else
            LOGPTAG(IHSHIPFRML,ihl1.mCurrentCarIndex);
        }
        got = crbo.add(ihl1.mCurrentCarIndex); // hand control back to comm
        HBPTAG(IHBShipi,ihl1.mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      ihl1.setupNewCar(crbi);                                   
    } else if (curihb == 0) { // if have no car
      if (!crbi.isEmpty()) { // ready to init?
        HBPTAG(IHEPucD8,ngbidx);
        ihl1.setupNewCar(crbi) ;
        HBPTAG(ihl1,ihl1.mCurrentCarIndex);
      } else HBNOTE(MT-CRBI);
    } else {
      HBNOTE("uncovered?");
    }

  }

  void InterHubEP::initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    HBPTAG(IHPT,getNameFromIHPType(IHPT_PING));
    HBPX(sizeof(IHPPing));
    HBPX(sizeof(IHPPing::HopReport));
    HBPX(sizeof(IHPAtoms));
    HBPX(sizeof(InterHubPayload));
    HBPX(sizeof(InterHubBlock));
    HBPX(sizeof(InterHubStorage));
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool InterHubEP::shipTC(SUBTC & car, u8 carindex) {
    ASSERT_RIGHT_HART();

    /* Plan: Intervene if this IH car needs a bankshot to reach our NE
       or SW corner. Then dispatch to shipTCTo in any case. */
    
    const EndPointAddress sEPA = getSrcEPA();
    u8 d8 = sEPA.mBlockCodeIndex;
    MFM_API_ASSERT(d8 <= D8_NE, ILLEGAL_STATE);
    const char * n = dir8ToByteString((Dir8) d8);
    EACH(1,{ if (d8&1) LOGPTAG(IHEPsTCDIVERT,n); else LOGPTAG(IHEPsTC,n); });

    u64 destcaraddr = mDestBlockAddr + CAR_SIZE*carindex;

    U8C ournoc0 = fAll.mNoC0;
    U8C destnoc0 = mDestNoC0;
    return shipTCTo(car,ournoc0,destnoc0,destcaraddr);
  }

  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    EACH(1,HBPTAG(IHrecvTC,__EACHNUM__));

    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    crbi.add(carindex); //notify hB

    EACH(1,HBPTAG(IHrecvTCD,carindex));

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    if (carindex >= CAR_COUNT) return 0;
    LOGPTAG(IHGotp,&this->getCarStg());
    return &this->getCarStg().getTC(carindex);
  }

}
