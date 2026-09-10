#include "EP_LogBlock.h"
#include "HartTasksLib.h" // for HTFuncPtr
#include "FastT0.h" // for totalMillisElapsed
#include "HostBlock.h" // for theHostBlock

namespace MFM {
  extern HostBlock theHostBlock;

  T6EPL1Data<LogBlockStg,1> theLogBlockL1Data;
  LogBlockL1Control theLogBlockL1Control;

  FAST_LOCAL(LogBlockEP,myLogBlockEPNC,n);

  bool LogBlockL1Control::writeMark(u16 fileid, u16 lineno, const char * msg) {
    /// ONLY LOG FROM T15!
    // if (theHostBlock.mTLBI != 15) return true;
    //    HBMARK;
    constexpr u32 HDRBYTES = 8u;
    HostBlock & hb = theHostBlock;
    u32 msglen = 0;
    for (const char * p = msg; *p; ++p) { ++msglen; }
    MFM_API_ASSERT(msglen < 250-HDRBYTES, ILLEGAL_ARGUMENT);
    u8 p256len = HDRBYTES + msglen;
    //SNAP(2,HBPTAG(LB1wM10,msg));
    do {
      AtomicScopeLock guard(mLock);
      if (!mCurrentLogBlock) return false;
      //SNAP(3,HBPTAG(LB1wM11,msg));
      LogBlock & lb = *mCurrentLogBlock;
      LogBlockPayload & pay = lb.payload();
      u32 br = pay.getBytesRemaining();
      //SNAP(3,HBPTAG(LBR,br));
      if (br <= p256len) {
        if (br < 2*HDRBYTES)
          return false; // marks missed NYI
        p256len = br;
        msglen = p256len-HDRBYTES;
      }

      u32 nowms = millisElapsed();
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
      for (u32 i = 0; i < msglen; ++i) {
        pay.put_u8(msg[i]);
      }
      //SNAP(5,HBPTAG(LBM,msg));
    } while(0);
    return true;
  }

  void LogBlockL1Control::init() {
    memset_s(this,'\0',sizeof(*this)); // init lock, mark no current, no base ticks
    //HBPTAG(LB1INITGO,sizeof(*this));
    // now wait until hn at least inits EP_LogBlock
    u32 spin = 0;
    while (theLogBlockL1Data.getPublicEPState(0) < EPState::INITTED) {
      ++spin;
      if (spin%20'000 == 0) {
        HBPTAG(LB1WAITP,spin);
        HBPTAG(LB1WAITS,theLogBlockL1Data.getPublicEPState(0));
      }
    }
    HBPTAG(LB1INITOUThb,spin);
    //LOGPTAG(LB1INITOUT,spin);
  }

  bool LogBlockL1Control::readyToClose() {
    //    HBPTAG(LB1CrTC10,mCurrentLogBlock);

    if (!mCurrentLogBlock)
      return false;             // nothing to close

    if (mLastTickOffset > LogBlockPayload::LBP_HIGH_TICKS_MARK) {
      HBPTAG(CLOGtime,mLastTickOffset);
      return true;              // close for lack of time
    }

    LogBlockPayload & pay = mCurrentLogBlock->payload();
    if (pay.mDataUsed > LogBlockPayload::LBP_HIGH_BYTES_MARK) {
      HBPTAG(CLOGspace,pay.mDataUsed);
      HBPTAG(y2?,this);
      return true;              // close for lack of space
    }

    //    if (pay.mDataUsed > 0) HBPTAG(LB1CrTCOP,pay.mDataUsed);
    return false;               // stay open
  }

  void LogBlockL1Control::setupNewCar(TheL1Data::CarIdxRB & crbi) { // RECEIVING
    u8 newcarindex;
    bool got = crbi.remove(newcarindex);
    HBASSERT_EQ(got,true);
    SNAP(4,HBPTAG(LBSUNCi,newcarindex));
        
    LogBlockStg & lbs = theLogBlockL1Data.mTheTCStorages[0];
    HBASSERT_LT(newcarindex, lbs.getCarCount());
    LogBlock & nlb = lbs.getTC(newcarindex);
    HBASSERT_EQ(nlb.getTCState(), TCState::OPEN); 
        
    mCurrentLogBlock = &nlb;
    mCurrentCarIndex = newcarindex;
    mLastTickOffset = 0;
    mBaseTicks = millisElapsed();
    mMarksMissed = 0;

    //    HBPTAG(LB1CsuNC,mCurrentLogBlock);
    LogBlockPayload & npay = nlb.payload();
    npay.reset(mBaseTicks); // no 'outside cmd processing' yet so just wipe it
  }

  int LogBlockL1Control::step(HostBlock & hb) {
    
    TheL1Data::CarIdxRB & crbi = theLogBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theLogBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    EACH(1'000,HBPTAG(L1CSt,mCurrentLogBlock));
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
      SNAP(4,HBPTAG(LB1BOOM,readyToClose()));
      AtomicScopeLock guard(mLock); // take the lock
      bool got;
      { // SHIPPING
        LogBlock & olb = *mCurrentLogBlock;
        LogBlockPayload & opay = olb.payload();
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        HBPTAG(LB->HN,mCurrentCarIndex);
        got = crbo.add(mCurrentCarIndex); // hand control back to comm
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      setupNewCar(crbi);                                   
    } else if (mCurrentLogBlock == 0 && !crbi.isEmpty()) { // ready to init?
      setupNewCar(crbi) ;
      SNAP(3,HBPTAG(logSNC,&crbi));
    } else {                    // not ready for anything
      //sleepCycles(500);
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

  static RCFlag manageLogBlockNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      HBPTAG(INIT@,__FUNCTION__);
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
      ret = RC_N_STARTED;

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      LOGMARK;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      {
        static u32 spin = 0;
        if ((++spin & 0xf'ffff) == 0)
          HBXTAG(logLive,spin);
      }

      //// LIFE
      {
        constexpr u32 BS = 40;
        char buf[BS];
        //        SNAP(3,HBPTAG(EPlogUO,myLogBlockEPNC.report(BS,buf)));
      }
      myLogBlockEPNC.updateOps();
    } else LOGPTAG(unknown htoc,htoc);
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr logEPPtr = &manageLogBlockNC;

  extern RCFlag manageLogBlockT0(HTOpCode htoc);
  ////////
  TEFResult TaskEpochFunction_LOG(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    switch (hei) {

    case HE_BORN1:
      if (hartnum != HARTNUM_T0) return TEFR_NO_THANKS;
      HBPTAG(@,__FUNCTION__);
      manageLogBlockT0(HTOC_INIT);
      return TEFR_CONTINUE;

    case HE_GROW0:
      HBNOTE(LT);
      LOGNOTE(LOGTESTLOG);
      return TEFR_CONTINUE;

    default:
      SNAP(2,HBPTAG(TEFLOG,getHartEpochName(hei)));
      break;
    }
    return TEFR_NO_THANKS;
  }
  

}
