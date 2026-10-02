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

  void InterHubPrivateControl::init(InterHubL1Control & ihl1) {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    memset_s(this,'\0',sizeof(*this)); // init all
    mIHL1Control = &ihl1;
    mPrivateState = IHH_PAUSE;
    HBPTAG(PIHinit,getIHHStateName(mPrivateState));
  }

  static bool processHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    {
      static u32 spin = 0u;
      if ((++spin & 0x7f'ffff) == 0) {
        HBXTAG(hubPrcHC,spin);
        LOGXTAG(hubPrcHCL,spin);
      }
    }

    if (theEwpL1Data.isUninitted(ngbidx)) return false;

    if (!theEwpL1Data.isActive(ngbidx)) {
      HBPTAG(hPROCBLOC,ngbidx);
      HBPVAL(getNameFromEPState(theEwpL1Data.getPublicEPState(ngbidx)));
      return false;             // maybe wait a bit
    }

    using EwpData = T6EPL1Data<EwpBlockStg,8>;
    EwpData::CarIdxs & idxs = theEwpL1Data.mTheCarIdxs[ngbidx];
    EwpBlockStg & cars = theEwpL1Data.mTheTCStorages[ngbidx];

    EwpData::CarIdxRB & crbi = idxs.mTheIdxs[EwpData::CarIdxs::COMM2COMP];
    EwpData::CarIdxRB & crbo = idxs.mTheIdxs[EwpData::CarIdxs::COMP2COMM];

    u8 carindex;
    if (!crbi.remove(carindex)) return false; // no arriving cars
    {
      static u32 spin = 0u;
      if (((spin++) & 0xfffff) == 0)
        HBXTAG(hub_prcHC,spin);
    }

    HBASSERT_LT(carindex, cars.getCarCount());
    EwpBlock & car = cars.getTC(carindex);
    HBASSERT_EQ(car.getTCState(), TCState::OPEN); 
    EwpPayload & pay = car.payload();

    fB.mGridManager.applyEWT(pay,ngbidx);

    u32 paysize = pay.currentPayloadSize();
    car.closeTC(paysize); // ready to go
    MFM_API_ASSERT(!crbo.isFull(),OUT_OF_ROOM);
    crbo.add(carindex);         // hand control back to comm
    return true;
  }

  void InterHubPrivateControl::stepB(HostBlock &hb) {
    EACH(1'000'000,HBPTAG(IHPC-stepB,getIHHStateName(mPrivateState)));
    L1GridManagerControl & lgmc = theL1GridManagerControl;
    InterHubL1Control & ihl1 = getL1();
    AtomicScopeLock guard(ihl1.mIHL1Lock);

    switch (mPrivateState) {
    case IHH_NONE:              // this is initial AND NULL state
      EACH(1'000'000,HBPTAG(NONETROM,getIHHStateName(mPrivateState)));
      break;

    case IHH_PAUSE:
      {
        if (!lgmc.isEPSuspStatus()) {
          HBPTAG(ACTIVATOTROM,1);
          LOGPTAG(ACTIVALOTROM,1);
          mPrivateState = IHH_RUN;
        }
      }
      break;

    case IHH_RUN:
      {
        if (lgmc.isEPSuspStatus()) {
          HBPTAG(ACTIVATOTROM,0);
          LOGPTAG(ACTIVALOTROM,0);
          mPrivateState = IHH_PAUSE;
        } else {
          EACH(1'000'000,HBPTAG(EVTOTROM,getIHHStateName(mPrivateState)));
          EACH(1'000'000,LOGPTAG(EVTOTROM,getIHHStateName(mPrivateState)));
          for (u32 e = 0u; e < 8u; ++e) {
            processHubCars(e,hb,true);
          }
        }
      }
      break;

    default:
      for (u32 i = 0u; i < 8u; ++i) {
        updateCars(i,hb,(i&1)==0);
      }
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

    EACH(1'000,HBPTAG(XXXpihUpCa,curihb));
    EACH(1'000,HBPTAG(YYYpihUpCa,ngbidx));
    EACH(1'000,HBPTAG(IIIpihUpCa,crbi.isEmpty()));
    EACH(1'000,HBPTAG(OOOpihUpCa,crbo.isEmpty()));

#if 0

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */

    bool keytest =
      curacb != 0 &&          // have a car and
      !crbi.isEmpty() &&      // more empty cars are available and
      !crbo.isFull() &&       // more full cars are shippable and
      acl1.readyToClose();
    EACH(1000,{HBPX(keytest);HBPX(curacb);HBPX(crbi.isEmpty());HBPX(crbo.isFull());HBPTAG(ACBPCuC,acl1.readyToClose());});
    if (keytest) {  // current car is ready to go
      bool got;

      if (false) {
        LOGPX(keytest);
        LOGPX(&acl1);

        HBPX(crbi.isEmpty());
        HBPX(crbo.isFull());
        HBPX(acl1.readyToClose());
      }
      

      { // SHIPPING
        InterHub & olb = *curacb;
        InterHubPayload & opay = olb.payload();
        HBPTAG(SHIPH,opay.getCurrentPayloadSize());
        HBPTAG(SHIPR,opay.getBytesRemaining());
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        if (false) {
          if (opay.getCurrentPayloadSize() < 4)
            LOGPTAG(SHIPFRMS,acl1.mCurrentCarIndex);
          else
            LOGPTAG(SHIPFRML,acl1.mCurrentCarIndex);
        }
        got = crbo.add(acl1.mCurrentCarIndex); // hand control back to comm
        HBPTAG(ACBShipi,acl1.mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      acl1.setupNewCar(crbi);                                   
    } else if (curacb == 0) { // if have no car
      if (!crbi.isEmpty()) { // ready to init?
        HBMARK;
        acl1.setupNewCar(crbi) ;
        HBPTAG(acl1,acl1.mCurrentCarIndex);
      } else HBNOTE(MT-CRBI);
    } else {
      // else not ready for anything
      //HBNOTE("uncovered?");
    }
#endif

  }

  void InterHubEP::initInterHubEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    HBPTAG(IHPT,getNameFromIHPType(IHPT_PING));
    HBPX(sizeof(IHPPing));
    HBPX(sizeof(IHPPing::HopReport));
    HBPX(sizeof(IHPAtoms));
    HBPX(sizeof(InterHubPayload));
    HBPX(sizeof(InterHubBlock));
    HBPX(sizeof(InterHubStorage));
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool InterHubEP::recvTC(InterHubBlock & car, u8 carindex) {
    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    if (crbi.isFull()) return false; // bail if can't notify??

    // Open it up
    car.openTC();
    crbi.add(carindex); //notify hB

    return true;
  }

  InterHubBlock * InterHubEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    //HBPTAG(IHGotp,&this->getCarStg());
    return &this->getCarStg().getTC(carindex);
  }

}
