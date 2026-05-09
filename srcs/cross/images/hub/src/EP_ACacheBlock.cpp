#include "EP_ACacheBlock.h"
#include "FastNC.h" // for EPFuncPtr
#include "FastT0.h" // for totalMillisElapsed
#include "HostBlock.h" // for theHostBlock
#include "Grid.h" // for DLGridList

namespace MFM {

  T6EPL1Data<ACacheBlockStg,1> theACacheBlockL1Data;
  ACacheBlockL1Control theACacheBlockL1Control;

  FAST_LOCAL(ACacheBlockEP,myACacheBlockEPNC,n);

  void ACacheBlockPrivateControl::init(ACacheBlockL1Control & acbl1, DLGridList & dll1) {
    memset_s(this,'\0',sizeof(*this)); // init all
    mACBL1Control = &acbl1;
    mDLGridList= &dll1;
    HBPTAG(PACBinit,sizeof(*this));
  }

  void ACacheBlockL1Control::init() {
    memset_s(this,'\0',sizeof(*this)); // init all
    HBPTAG(ACB1INITGO,sizeof(*this));
  }

  bool ACacheBlockL1Control::readyToClose() {
    if (!mCurrentACacheBlock)
      return false;             // nothing to close
    FAIL(INCOMPLETE_CODE);
    return false;               // stay open
  }

  void ACacheBlockL1Control::setupNewCar(TheL1Data::CarIdxRB & crbi) { // RECEIVING
    u8 newcarindex;
    bool got = crbi.remove(newcarindex);
    HBASSERT_EQ(got,true);
    HBPTAG(LBSUNCi,newcarindex);
        
    ACacheBlockStg & lbs = theACacheBlockL1Data.mTheTCStorages[0];
    HBASSERT_LS(newcarindex, lbs.getCarCount());
    ACacheBlock & nlb = lbs.getTC(newcarindex);
    HBASSERT_EQ(nlb.getTCState(), TCState::OPEN); 
        
    mCurrentACacheBlock = &nlb;
    mCurrentCarIndex = newcarindex;
    mBaseTicks = totalMillisElapsed;
    mAReportsMissed = 0;

    HBPTAG(ACB1CsuNC,mCurrentACacheBlock);
    ACacheBlockPayload & npay = nlb.payload();
    npay.reset();
  }

  int ACacheBlockL1Control::step(HostBlock & hb) {
    
    TheL1Data::CarIdxRB & crbi = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */
    if (true) {
      static u32 spin = 0;
      if (spin++ % 10000 == 0) {
        HBPTAG(ACB1CS10,mCurrentACacheBlock);
        HBPTAG(ACB1CS11,!crbi.isEmpty());
        HBPTAG(ACB1CS12,!crbo.isFull());
        HBPTAG(ACB1CS13,readyToClose());
      }
    }
    if (mCurrentACacheBlock != 0 &&   // have a car and
        !crbi.isEmpty() &&         // more empty cars are available and
        !crbo.isFull() &&          // more full cars are shippable and
        readyToClose()) {          // current car is ready to go
      HBPTAG(ACB1BOOM,readyToClose());
      AtomicScopeLock guard(mLock); // take the lock
      bool got;
      { // SHIPPING
        ACacheBlock & olb = *mCurrentACacheBlock;
        ACacheBlockPayload & opay = olb.payload();
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        got = crbo.add(mCurrentCarIndex); // hand control back to comm
        HBPTAG(LBShipi,mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      setupNewCar(crbi);                                   
    } else if (mCurrentACacheBlock == 0 && !crbi.isEmpty()) { // ready to init?
      HBMARK;
      setupNewCar(crbi) ;
    } else {                    // not ready for anything
      sleepCycles(500);
    }
    return 0;
  }

  void ACacheBlockEP::initACacheBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    LOGXTAG(InLBEP,&l1data);
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool ACacheBlockEP::recvTC(ACacheBlock & car, u8 carindex) {
    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    HBPTAG(ABRCV?,carindex);

    if (crbi.isFull()) return false; // bail if can't notify??

    HBPTAG(ABRCV!,carindex);
    car.openTC();               // open it up
    car.payload().init();       // clean it out
    HBPTAG(ABRCV*,car.getTCState());
    crbi.add(carindex);         // notify h1 (will access it in ACacheBlockL1Control::step(..) above)
    return true;
  }

  ACacheBlock * ACacheBlockEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

  static bool manageACacheBlockNC(bool doInit) {
    bool ret = false;
    if (unlikely(doInit)) {
      LOGMARK;

      theACacheBlockL1Data.reset();     // zero all
      auto & theACacheBlockStgs = theACacheBlockL1Data.mTheTCStorages;

      ///// BIRTH

      // Set up cars
      for (u32 i = 0; i < sizeof(theACacheBlockStgs)/sizeof(theACacheBlockStgs[0]); ++i) {
        ACacheBlockStg & lbs = theACacheBlockStgs[i];
        for (u32 c = 0; c < lbs.getCarCount(); ++c) {
          ACacheBlock & lb = lbs.getTC(c);
          lb.init();
        }
      }
      // Cars are now initted

      // Set up our endpoint: Source { ACACHEBLOCK, 0 }
      myACacheBlockEPNC.initACacheBlockEP({ BC_ACACHEBLOCK, 0 }, false, theACacheBlockL1Data);
      LOGPTAG(lbCFD, theACacheBlockL1Data.getPublicEPState(0));

      // Set up our endpoint: Dest { ACACHEBLOCK, ourtlbi? }
      u8 tlbi = (u8) U8C::makeTLBIFromNoCCoord(fAll.mNoC0);

      myACacheBlockEPNC.configureDest(fAll.mNoC0, PCIeTILE_NOC0, { BC_ACACHEBLOCK, tlbi });
      myACacheBlockEPNC.activate();

      HBPTAG(lbACT, theACacheBlockL1Data.getPublicEPState(0));
      ret = true;

    } else {

      //// LIFE
      //      SNAP(5,HBMARK);
      if (myACacheBlockEPNC.updateOps()) {
        ret = true;
      }
    }
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr acbEPPtr = &manageACacheBlockNC;

}
