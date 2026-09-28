#include "EP_ACacheBlock.h"
#include "HartTasks.h" // for HTFuncPtr
#include "FastT0.h" // for totalMillisElapsed
#include "HostBlock.h" // for theHostBlock
#include "Grid.h" // for DLGridList
#include "LiveT1.h"    // for mLZBytes*

namespace MFM {

  T6EPL1Data<ACacheBlockStg,1> theACacheBlockL1Data;
  ACacheBlockL1Control theACacheBlockL1Control;

  FAST_LOCAL(ACacheBlockEP,myACacheBlockEPNC,n);

  void ACacheBlockL1Control::init() {
    ONE_PING_ONLY();
    memset_s(this,'\0',sizeof(*this)); // init all
    HBPTAG(ACB1INITGO,sizeof(*this));
  }

  void ACacheBlockL1Control::setFlags(u8 flags) {
    MFM_API_ASSERT((flags&mFlags)==0,ILLEGAL_ARGUMENT);
    HBXTAG(ACBflagswas,(u32) mFlags);
    mFlags |= flags;
    memoryFence();
    HBXTAG(ACBFlagsNow,(u32) mFlags);
  }

  bool ACacheBlockL1Control::testFlags(u8 flags) const {
    memoryFence();
    return (mFlags & flags);
  }

  bool ACacheBlockL1Control::readyToClose() {
    AtomicScopeLock guard(mLock);
    if (!mCurrentACacheBlock)
      return false;             // nothing to close
    ACacheBlock & acb = *mCurrentACacheBlock;
    ACacheBlockPayload & pay = acb.payload();
    return
      pay.getBytesRemaining() == 0 || pay.checkRCloseFlag();
  }

  bool ACacheBlockL1Control::writeByteToCurrentACB(u8 byte) {
    AtomicScopeLock guard(mLock);
    if (!mCurrentACacheBlock) {
      LOGXTAG(noCuACB,(u32) byte);
      return false;             // nothing to write to
    }
    //    SNAP(100,LOGXTAG(HACUBLK,mCurrentACacheBlock));
    ACacheBlock & acb = *mCurrentACacheBlock;
    ACacheBlockPayload & pay = acb.payload();
    u32 wasused = pay.getCurrentLength();
    bool ret = pay.addByte(byte);
    if (ret) ++mCompressedBytesPacked;
    {
      if (ret /*true wasused < 3 || wasused > 990-3*/) {
        u32 nowused = pay.getCurrentLength();
        u32 idx = mCompressedBytesPacked-1;
        char buf[40];
        snprintf(buf,40,"#%u @%u:%u 0x%02x",idx,wasused,nowused,byte);
        if (!ret) SNAP(2,LOGPTAG(sWBC>,buf));
        else if ((idx % 990) > 980 || (idx % 990) <  20)
          EACH(100'000,LOGPTAG(ARCB,buf));
        //EACH(1,LOGPTAG(ARCB,buf));
      }
    }
    if (!ret) EACH(1'000,HBXTAG(BLODK,(u32)byte));
    return ret;
  }

  void ACacheBlockL1Control::setupNewCar(TheL1Data::CarIdxRB & crbi) { // RECEIVING FROM COMM2COMP
    AtomicScopeLock guard(mLock);

    constexpr u32 BS = 50;
    char buf[BS];

    u8 newcarindex;
    bool got = crbi.remove(newcarindex);
    HBASSERT_EQ(got,true);
    HBPTAG(ACBSUNC,newcarindex);
        
    ACacheBlockStg & lbs = theACacheBlockL1Data.mTheTCStorages[0];
    HBASSERT_LT(newcarindex, lbs.getCarCount());
    ACacheBlock & nlb = lbs.getTC(newcarindex);
    HBASSERT_EQ(nlb.getTCState(), TCState::OPEN); 
        
    mCurrentACacheBlock = &nlb;
    mCurrentCarIndex = newcarindex;
    mBaseTicks = millisElapsed();
    mAReportsMissed = 0;
    if (false) {
      LOGPX(mCurrentACacheBlock);
      LOGPX(mCurrentCarIndex);
      LOGPX(mBaseTicks);
    }

    ACacheBlockPayload & npay = nlb.payload();
    npay.reset();
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
    //    HBPTAG(ABRCV*,car.getTCState());
    //    HBPTAG(crbi,&crbi);
    //    HBPX(crbi.isEmpty());
    //    HBPX(crbi.isFull());
    crbi.add(carindex);         // notify h1 (will access it in ACacheBlockL1Control::step(..) above)
    return true;
  }

  ACacheBlock * ACacheBlockEP::getCarPtrIfAny(u8 carindex) const {
    if (carindex >= CAR_COUNT) return 0;
    return &this->getCarStg().getTC(carindex);
  }

  //////// H0 MSTICK HART TASK
  FAST_LOCAL(ACacheBlockPrivateControl,myPACBControlH0,0);

  void ACacheBlockPrivateControl::init(ACacheBlockL1Control & acbl1, DLGridList & dll1) {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    HBNOTE(acbpriv2pubinit);
    memset_s(this,'\0',sizeof(*this)); // init all
    mACBL1Control = &acbl1;
    mDLGridList= &dll1;
    mARIO.init(true);   // true: init for serialization (obj -> bytes)
    HBPTAG(PACBinit,sizeof(*this));
  }

  void ACacheBlockPrivateControl::updateCars(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    
    TheL1Data::CarIdxRB & crbi = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    MFM_API_ASSERT_NONNULL(mACBL1Control);
    ACacheBlockL1Control & acl1 = *mACBL1Control;
    ACacheBlock * curacb = acl1.mCurrentACacheBlock; // if any

    EACH(1'000,HBPTAG(privUpCa,curacb));
    /*

    HBPX(!crbi.isEmpty());
    HBPX(!crbo.isFull());
    HBPX(acl1.readyToClose());
    */

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
        ACacheBlock & olb = *curacb;
        ACacheBlockPayload & opay = olb.payload();
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
  }

  int ACacheBlockPrivateControl::step(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);

    EACH(10'000,HBXX(mDLGridList));
    EACH(10'000,HBXX(mACBL1Control));
    MFM_API_ASSERT_NONNULL(mDLGridList);
    MFM_API_ASSERT_NONNULL(mACBL1Control);
    EACH(10'000,HBPTAG(ACBS11,__EACHNUM__));
    if (true) {
      static u32 spin = 0u;
      if ((spin++ & 0xffff) == 0) {
        LOGPTAG(ACBPCstep,spin>>16);
        LOGPTAG(DGLLen,mDLGridList->getLength());
        LOGPX((u32) mState);
      }
    }
    EACH(10'000,HBNOTE(ACBS12));
    updateCars(hb);
    EACH(10'000,HBNOTE(ACBS13));
    tryToSendFrame(hb);
    EACH(10'000,HBPTAG(ACBS14,__EACHNUM__));
    return 0;
  }

  ACacheBlock & ACacheBlockPrivateControl::getCurrentACBOrDie() {
    MFM_API_ASSERT_NONNULL(mACBL1Control);
    ACacheBlockL1Control & acl1 = *mACBL1Control;
    MFM_API_ASSERT_NONNULL(acl1.mCurrentACacheBlock);
    ACacheBlock & acb = *acl1.mCurrentACacheBlock;
    return acb;
  }

  const char * ACacheBlockPrivateControl::getStateName(State us) {
    switch (us) {
    case UNINIT: return "Uninit.0";
    case NEXT: return "Next.1";
    case WAIT: return "Wait.2";
    case PACK: return "Pack.3";
    }
    FAIL(UNREACHABLE_CODE);
    return "";
  }

  void ACacheBlockPrivateControl::tryToSendFrame(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);

    SNAP(100,HBPTAG(mSt,getStateName(mState)));
    MFM_API_ASSERT_NONNULL(mDLGridList);
    DLGridList & dl = *mDLGridList;
    switch (mState) {
    case State::UNINIT:
      {
        mState = State::WAIT;
        mNextFrameBogoMS = millisElapsed();
      }
      break;
    case State::NEXT:
      {
        ACacheBlock &acb = getCurrentACBOrDie();
        
        // WAIT FOR RCLOSE CLEAR
        if (!acb.payload().checkRCloseFlag()) {
          mFramesSent++;
          if (false) {
            LOGPTAG(2PACK,mFramesSent);
            HBPTAG(2PACK,mFramesSent);
          }
          mNextFrameBogoMS += BOGOMS_PER_FRAME;
          if ((mFramesSent%100) == 0) LOGPTAG(WAITSENT,mFramesSent);
          mARsToSend = dl.getLength();
          mState = State::PACK;
        }
      }
      break;

    case State::WAIT:
      {
        // Check if we're go to suspend
        if (!theL1GridManagerControl.mEventProcessingSuspendStatus && // not already suspended
            theL1GridManagerControl.mEventProcessingSuspendRequest && // but a request has been made
            theL1GridManagerControl.mEventProcessingSuspendRequestSeen && // and applyEWT has seen it
            dl.getLength() == 0) { // and we have nothing more to send
          theL1GridManagerControl.mEventProcessingSuspendStatus = true;
          LOGPTAG(HERBO_SUSPACH,theL1GridManagerControl.mEventProcessingSuspendStatus);
        } else if (false && theL1GridManagerControl.mEventProcessingSuspendStatus && // if already suspended
                   !theL1GridManagerControl.mEventProcessingSuspendRequest) { // but no request is active
          theL1GridManagerControl.mEventProcessingSuspendStatus = false;
          LOGPTAG(HERBO_NOSUSPACH,dl.getLength());
        }

        if (mNextFrameBogoMS <= millisElapsed()) {
          //          LOGPTAG(2NEXT,mNextFrameBogoMS);
          HBPTAG(2NEXT,mNextFrameBogoMS);

          // ACCESS PAY
          ACacheBlock &acb = getCurrentACBOrDie();

          // SET RCLOSE
          //XXX LET'S FILL THE FOGGENCAR          acb.payload().setRCloseFlag();

          // WAIT TIL SHIPT? how?
          mState = State::NEXT;
        }
      }
      break;

    case State::PACK:
      {
        //        LOGPTAG(PACKI,mARsToSend);
        if (mNextFrameBogoMS <= millisElapsed()) {
          if (false) LOGPTAG(EOFTM,mFramesSent);
          mState = State::WAIT; // ran out of frame time
        } else {

          // PACK WHAT BYTES WE CAN
          AtomReport ar;
          T6Grid & t6g = theT6Grid[0];
          u32 count = 0;

          //LOGPTAG(PAKLEN,l1dLZBytesIn.lengthish());
          while (!l1dLZBytesIn.isFull()) { // while Room For One More

            /// (1) Does mARIO have more bytes to supply?
            u8 byte;
            if (mARIO.tryGetByte(byte)) {
              // Yes
              l1dLZBytesIn.add(byte);
              ++count;
              ++mUncompressedBytesPacked;
              {
                char buf[20];
                snprintf(buf,20,"#%u 0x%02x",mUncompressedBytesPacked,byte);
                //EACH(1,LOGPTAG(ARUB,buf));
              }
              continue;
            }
            // No, done with that object
            mARIO.discardObj();
            
            /// (2) Does dl have more ARs to supply?
            U8C arc;
            if (dl.popBackC(arc)) {
              // Yes. Make report
              ar.mCoord = U16C(arc.x,arc.y);
              ar.mSpin1 = ar.mSpin2 = mARSpinner++;
              ar.mAtom = t6g.getAtom(ar.mCoord);
              //EACH(1,{LOGPTAG(AC:,ar.mCoord);LOGATOM(ar.mAtom);});
              
              // Stash it (or die: We Just Cleared mARIO)
              mARIO.putObj(ar);
              continue;
            }
            /// (3) We have to wait
            mState = State::WAIT; // nothing else to send
            break;                // stop the loop
          }
          //LOGPTAG(PKOUT,count);
        }
      }
      break;
    default:
      FAIL(UNREACHABLE_CODE);
    }
  }

  ////////
  RCFlag manageACacheBlockT0(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      ONE_PING_ONLY();
      //HBNOTE("ACBT0ARO");

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      ONE_PING_ONLY();

      LOGMARK;
      HBNOTE(ACBOP!);
      ACacheBlockL1Control & acbl1 = theACacheBlockL1Control;
      static u32 spin = 0u;
      while (!acbl1.testFlags(acbl1.ACBL1_NC_INITTED)) {
        if ((++spin % 100'000) == 0)
          HBPTAG(t0NCWait,spin);
      }
      HBPTAG(NCDONE!,spin);

      //// ONE-TIME INITS
      myPACBControlH0.init(theACacheBlockL1Control,theDLGridList);
      acbl1.setFlags(acbl1.ACBL1_T0_INITTED);
      LOGXTAG(T0INT,(u32)acbl1.getFlags());

      ret = RC_0_SELF_UP;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      EACH(1'000,HBPTAG(ACBLIV,__EACHNUM__));
      //// LIVING
      HostBlock & hb = theHostBlock;
      ACacheBlockPrivateControl & privcH0 = myPACBControlH0;
      privcH0.step(hb);
      EACH(1'000,HBPTAG(ACBLOV,__EACHNUM__));

      static u32 spin = 0u;
      if ((++spin % 10'000) == 0)
        HBPTAG(+T0ACBLIVE,spin);
    } else LOGPTAG(unknown htoc,htoc);

    return ret;
  }

  static RCFlag stepACBsT0(HTOpCode htoc) {
    if (htoc == HTOC_LIVE) {
      HostBlock & hb = theHostBlock;
      myPACBControlH0.step(hb);
    }
    return RC_ZERO;
  }

  __attribute__((section(".rodata_fp_table_t0")))
  HTFuncPtr ACBStepFuncPtr = &stepACBsT0;

  /// Sat Jul 25 22:59:15 2026 XXX TEST  __attribute__((section(".rodata_fp_table_t0")))
  HTFuncPtr ACacheBlockEPPtrT0 = &manageACacheBlockT0;

  
  ////////
  static RCFlag manageACacheBlockNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (htoc == HTOpCode::HTOC_INIT) {
      HBNOTE("MACBARO");
      LOGMARK;
      ONE_PING_ONLY();

      theACacheBlockL1Data.reset();     // zero all

      ACacheBlockL1Control & acbl1 = theACacheBlockL1Control;
      acbl1.init();             // init public state

      auto & theACacheBlockStgs = theACacheBlockL1Data.mTheTCStorages; // compiler knows the foggn array size

      ///// BIRTH

      // Set up cars
      for (u32 i = 0; i < sizeof(theACacheBlockStgs)/sizeof(theACacheBlockStgs[0]); ++i) {
        ACacheBlockStg & acbs = theACacheBlockStgs[i];
        for (u32 c = 0; c < acbs.getCarCount(); ++c) {
          ACacheBlock & acb = acbs.getTC(c);
          acb.init();
          HBPTAG(carsACBS,c);
          //          HBPTAG(acbState,(u32) acb.getTCState());
          //          HBPTAG(mGoneCount1,myACacheBlockEPNC.getGoneCount());
        }
      }
      // Cars are now initted

      // Set up our endpoint: Source { ACACHEBLOCK, 0 }
      myACacheBlockEPNC.initACacheBlockEP({ BC_ACACHEBLOCK, 0 }, false, theACacheBlockL1Data);
      LOGPTAG(eacbCFD, theACacheBlockL1Data.getPublicEPState(0));
      HBPTAG(mGC2,myACacheBlockEPNC.getGoneCount());

      // Set up our endpoint: Dest { ACACHEBLOCK, ourtlbi? }
      u8 tlbi = (u8) U8C::makeTLBIFromNoCCoord(fAll.mNoC0);

      myACacheBlockEPNC.configureDest(fAll.mNoC0, PCIeTILE_NOC0, { BC_ACACHEBLOCK, tlbi });
      myACacheBlockEPNC.activate();

      HBPTAG(eacbACT, theACacheBlockL1Data.getPublicEPState(0));

      // We're done
      acbl1.setFlags(acbl1.ACBL1_NC_INITTED);
      HBXTAG(acbl1Flags, (u32) acbl1.getFlags());
      ret = RC_N_SELF_UP; // XXX this has to be wrong. we are a service not all of HN
    } else if (htoc == HTOpCode::HTOC_OPEN) {
      LOGMARK;
      ONE_PING_ONLY();
      myACacheBlockEPNC.updateOps();
      HBPTAG(ACBOP,myACacheBlockEPNC.getCarPtrIfAny(0));
    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {
      
      //// LIFE
      // ((DON'T LIVE WHILE WE'RE INITTING))
      EACH(1'000,HBPTAG(DontLive,__EACHNUM__));
    } else LOGPTAG(unknown htoc,htoc);
    return ret;
  }

  static RCFlag manageACacheBlockNCForLiving(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO; 

    if (htoc != HTOpCode::HTOC_LIVE) return ret;
    
    if (theHostBlock.mPerHartStatus[fAll.mHartNum] != FAILCode::INITTING) {
      //EACH(1'000,LOGPTAG(live,__EACHNUM__));
      myACacheBlockEPNC.updateOps();
    }

    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr acbEPPtr = &manageACacheBlockNCForLiving;

  extern int myInitT1() ;
  extern int myLiveT1(HostBlock & hb) ;

  ////////
  TEFResult TaskEpochFunction_HACT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    switch (hei) {
    case HE_BORN1:
      if (hartnum == HARTNUM_B) {
        extern int myInitB();
        myInitB();
        return TEFR_CONTINUE;
      }
      if (hartnum == HARTNUM_NC) {
        manageACacheBlockNC(HTOC_INIT);
        return TEFR_CONTINUE;
      }
      return TEFR_NO_THANKS;

    case HE_GROW0: //case HE_BORN0:
      HBNOTE(iHACT);
      if (hartnum == HARTNUM_T0) {
        manageACacheBlockT0(HTOC_INIT);
      } else if (hartnum == HARTNUM_T1) {
        myInitT1();
      } else break;
      HBMARK;
      return TEFR_CONTINUE;

    case HE_GROW1: //case HE_BORN1:
      HBNOTE(openHACT);
      if (hartnum == HARTNUM_T0) {
        manageACacheBlockT0(HTOC_OPEN);
      } else if (hartnum == HARTNUM_T1) {
        HBNOTE("T1 nongo");
      } else if (hartnum == HARTNUM_NC) {
        manageACacheBlockNC(HTOC_OPEN);
      } else break;
      HBMARK;
      return TEFR_CONTINUE;

    default:
      LOGMARK;
      break;
    }
    return TEFR_NO_THANKS;
  }

}
