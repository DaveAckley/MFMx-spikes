#include "EP_LogBlock.h"
#include "FastNC.h" // for EPFuncPtr
#include "FastT0.h" // for totalMillisElapsed
#include "HostBlock.h" // for theHostBlock

namespace MFM {
  extern HostBlock theHostBlock;

  T6EPL1Data<LogBlockStg,1> theLogBlockL1Data;
  LogBlockL1Control theLogBlockL1Control;

  FAST_LOCAL(LogBlockEP,myLogBlockEPNC,n);

  bool LogBlockL1Control::writeMark(u16 fileid, u16 lineno, const char * msg) {
    //    HBMARK;
    constexpr u32 HDRBYTES = 8u;
    HostBlock & hb = theHostBlock;
    u32 msglen = 0;
    for (const char * p = msg; *p; ++p) { ++msglen; }
    MFM_API_ASSERT(msglen < 250-HDRBYTES, ILLEGAL_ARGUMENT);
    u8 p256len = HDRBYTES + msglen;
    //    HBPTAG(LB1wM10,p256len);
    do {
      AtomicScopeLock guard(mLock);
      if (!mCurrentLogBlock) return false;
      LogBlock & lb = *mCurrentLogBlock;
      LogBlockPayload & pay = lb.payload();

      if (pay.getBytesRemaining() <= p256len) return false; // marks missed NYI

      u32 nowms = totalMillisElapsed;
      u32 diff = nowms - mBaseTicks;
      if (diff >= U16_MAX) return false; 

      u16 diff16 = (u16) diff;
      mLastTickOffset = diff16;

      // We are going to succeed. Write the packet:
      pay.put_u8(p256len);
      pay.put_u8(fAll.mHartNum); // cmd NYI
      pay.put_u16(diff16);
      pay.put_u16(fileid);
      pay.put_u16(lineno);
      for (const char * p = msg; *p; ++p) {
        pay.put_u8(*p);
      }
    } while(0);
    return true;
  }

  void LogBlockL1Control::init() {
    memset_s(this,'\0',sizeof(*this)); // init lock, mark no current, no base ticks
    HBPTAG(LB1INITGO,sizeof(*this));
    // now wait until hn at least inits EP_LogBlock
    u32 spin = 0;
    while (theLogBlockL1Data.getPublicEPState(0) < EPState::INITTED) {
      ++spin;
      if (spin%20'000 == 0) {
        HBPTAG(LB1WAITP,spin);
        HBPTAG(LB1WAITS,theLogBlockL1Data.getPublicEPState(0));
      }
    }
    LOGPTAG(LB1INITOUT,spin);
  }

  bool LogBlockL1Control::readyToClose() {
    //    HBPTAG(LB1CrTC10,mCurrentLogBlock);

    if (!mCurrentLogBlock)
      return false;             // nothing to close

    //HBPTAG(LB1CrTC11,mLastTickOffset);
    if (mLastTickOffset > LogBlockPayload::LBP_HIGH_TICKS_MARK)
      return true;              // close for lack of time

    LogBlockPayload & pay = mCurrentLogBlock->payload();
    //    HBPTAG(LB1CrTC12,pay.mDataUsed);
    if (pay.mDataUsed > LogBlockPayload::LBP_HIGH_BYTES_MARK)
      return true;              // close for lack of space

    //    if (pay.mDataUsed > 0) HBPTAG(LB1CrTCOP,pay.mDataUsed);
    return false;               // stay open
  }

  void LogBlockL1Control::setupNewCar(TheL1Data::CarIdxRB & crbi) { // RECEIVING
    u8 newcarindex;
    bool got = crbi.remove(newcarindex);
    HBASSERT_EQ(got,true);
    //    HBPTAG(LBSUNCi,newcarindex);
        
    LogBlockStg & lbs = theLogBlockL1Data.mTheTCStorages[0];
    HBASSERT_LS(newcarindex, lbs.getCarCount());
    LogBlock & nlb = lbs.getTC(newcarindex);
    HBASSERT_EQ(nlb.getTCState(), TCState::OPEN); 
        
    mCurrentLogBlock = &nlb;
    mCurrentCarIndex = newcarindex;
    mLastTickOffset = 0;
    mBaseTicks = totalMillisElapsed;
    mMarksMissed = 0;

    //    HBPTAG(LB1CsuNC,mCurrentLogBlock);
    LogBlockPayload & npay = nlb.payload();
    npay.reset(mBaseTicks); // no 'outside cmd processing' yet so just wipe it
  }

  int LogBlockL1Control::step(HostBlock & hb) {
    
    TheL1Data::CarIdxRB & crbi = theLogBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theLogBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    //HBPTAG(LB1CStep10,mCurrentLogBlock);
    //    LOGNOTE("USE UP CURRENT LOG BLOCK! FASTER PUSSYCAT FASTER!");
    //    LOGPTAG(LB1CStep10,mCurrentLogBlock);

    if (false) {
      static u32 spin = 0;
      if (spin++ % 10000 == 0) {
        HBPTAG(LB1CS10,mCurrentLogBlock);
        HBPTAG(LB1CS11,!crbi.isEmpty());
        HBPTAG(LB1CS12,!crbo.isFull());
        HBPTAG(LB1CS13,readyToClose());
      }
    }

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */
    if (mCurrentLogBlock != 0 &&   // have a car and
        !crbi.isEmpty() &&         // more empty cars are available and
        !crbo.isFull() &&          // more full cars are shippable and
        readyToClose()) {          // current car is ready to go
      //      HBPTAG(LB1BOOM,readyToClose());
      AtomicScopeLock guard(mLock); // take the lock
      bool got;
      { // SHIPPING
        LogBlock & olb = *mCurrentLogBlock;
        LogBlockPayload & opay = olb.payload();
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        got = crbo.add(mCurrentCarIndex); // hand control back to comm
        //        HBPTAG(LBShipi,mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      setupNewCar(crbi);                                   
    } else if (mCurrentLogBlock == 0 && !crbi.isEmpty()) { // ready to init?
      HBMARK;
      setupNewCar(crbi) ;
    } else {                    // not ready for anything
      sleepCycles(500);
    }
    return 0;
  }

  void LogBlockEP::initLogBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    LOGXTAG(InLBEP,&l1data);
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool LogBlockEP::recvTC(LogBlock & car, u8 carindex) {
    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    //    HBPTAG(LBRCV?,carindex);

    if (crbi.isFull()) return false; // bail if can't notify??

    //    HBPTAG(LBRCV!,carindex);
    car.openTC();               // open it up
    car.payload().init();       // clean it out
    //    HBPTAG(LBRCV*,car.getTCState());
    crbi.add(carindex);         // notify h1 (will access it in LogBlockL1Control::step(..) above)
    return true;
  }

  LogBlock * LogBlockEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

  static bool manageLogBlockNC(bool doInit) {
    bool ret = false;
    if (unlikely(doInit)) {
      LOGMARK;

      theLogBlockL1Data.reset();     // zero all
      auto & theLogBlockStgs = theLogBlockL1Data.mTheTCStorages;

      ///// BIRTH

      // Set up cars
      for (u32 i = 0; i < sizeof(theLogBlockStgs)/sizeof(theLogBlockStgs[0]); ++i) {
        LogBlockStg & lbs = theLogBlockStgs[i];
        for (u32 c = 0; c < lbs.getCarCount(); ++c) {
          LogBlock & lb = lbs.getTC(c);
          lb.init();
        }
      }
      // Cars are now initted

      // Set up our endpoint: Source { LOGBLOCK, 0 }
      myLogBlockEPNC.initLogBlockEP({ BC_LOGBLOCK, 0 }, false, theLogBlockL1Data);
      LOGPTAG(lbCFD, theLogBlockL1Data.getPublicEPState(0));

      // Set up our endpoint: Dest { LOGBLOCK, ourtlbi? }
      u8 tlbi = (u8) U8C::makeTLBIFromNoCCoord(fAll.mNoC0);

      myLogBlockEPNC.configureDest(fAll.mNoC0, PCIeTILE_NOC0, { BC_LOGBLOCK, tlbi });
      myLogBlockEPNC.activate();

      HBPTAG(lbACT, theLogBlockL1Data.getPublicEPState(0));
      ret = true;

    } else {

      //// LIFE
      //      SNAP(5,HBMARK);
      if (myLogBlockEPNC.updateOps()) {
        ret = true;
      }
    }
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr logEPPtr = &manageLogBlockNC;

}
