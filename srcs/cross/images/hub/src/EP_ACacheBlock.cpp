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
    memset_s(this,'\0',sizeof(*this)); // init all
    HBPTAG(ACB1INITGO,sizeof(*this));
  }

  void ACacheBlockL1Control::setFlags(u8 flags) {
    MFM_API_ASSERT((flags&mFlags)==0,ILLEGAL_ARGUMENT);
    //HBXTAG(ACBflagswas,(u32) mFlags);
    mFlags |= flags;
    memoryFence();
    HBXTAG(ACBFlagsNow,(u32) mFlags);
  }

  bool ACacheBlockL1Control::testFlags(u8 flags) const {
    memoryFence();
    return (mFlags & flags);
  }

  bool ACacheBlockL1Control::readyToClose() {
    if (!mCurrentACacheBlock)
      return false;             // nothing to close
    ACacheBlock & acb = *mCurrentACacheBlock;
    ACacheBlockPayload & pay = acb.payload();
    return
      pay.getBytesRemaining() == 0 || pay.checkRCloseFlag();
  }

  bool ACacheBlockL1Control::writeByteToCurrentACB(u8 byte) {
    AtomicScopeLock guard(mLock);
    if (!mCurrentACacheBlock)
      return false;             // nothing to write to
    //    SNAP(100,LOGXTAG(HACUBLK,mCurrentACacheBlock));
    ACacheBlock & acb = *mCurrentACacheBlock;
    ACacheBlockPayload & pay = acb.payload();
    bool ret = pay.addByte(byte);
    if (ret) SNAP(10,LOGPTAG(WBTACB,pay.getCurrentLength()));
    else SNAP(10,LOGXTAG(BLODK,(u32)byte));
    return ret;
  }

  void ACacheBlockL1Control::setupNewCar(TheL1Data::CarIdxRB & crbi) { // RECEIVING FROM COMM2COMP
    u8 newcarindex;
    bool got = crbi.remove(newcarindex);
    HBASSERT_EQ(got,true);
    HBPTAG(ACBSUNC,newcarindex);
        
    ACacheBlockStg & lbs = theACacheBlockL1Data.mTheTCStorages[0];
    HBASSERT_LS(newcarindex, lbs.getCarCount());
    ACacheBlock & nlb = lbs.getTC(newcarindex);
    HBASSERT_EQ(nlb.getTCState(), TCState::OPEN); 
        
    mCurrentACacheBlock = &nlb;
    mCurrentCarIndex = newcarindex;
    mBaseTicks = millisElapsed();
    mAReportsMissed = 0;

    LOGPTAG(RECVHST,mCurrentCarIndex);
    ACacheBlockPayload & npay = nlb.payload();
    npay.reset();
  }

#if 0
  int ACacheBlockL1Control::step(HostBlock & hb) {
    EACH(10'000,HBNOTE("ACBL1step"));

    TheL1Data::CarIdxRB & crbi = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */
    //^^^^ UNCLEAR IF WE BELIEVE THAT HERE. LET'S TAKE THE LOCK ^^^^^^
    AtomicScopeLock guard(mLock);
    //LOGPTAG(GOTLOCK,&mLock);

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
      bool got;
      { // SHIPPING
        ACacheBlock & olb = *mCurrentACacheBlock;
        ACacheBlockPayload & opay = olb.payload();
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        LOGPTAG(ACB1SHIP,mCurrentCarIndex);
        got = crbo.add(mCurrentCarIndex); // hand control back to comm
        HBPTAG(ACBShipi,mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      setupNewCar(crbi);                                   
    } else if (mCurrentACacheBlock == 0 && !crbi.isEmpty()) { // ready to init?
      HBMARK;
      setupNewCar(crbi) ;
    } // else not ready for anything

    return 0;
  }
#endif

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
    HBNOTE(acbpriv2pubinit);
    memset_s(this,'\0',sizeof(*this)); // init all
    mACBL1Control = &acbl1;
    mDLGridList= &dll1;
    mARIO.init();
    HBPTAG(PACBinit,sizeof(*this));
  }

  int ACacheBlockPrivateControl::updateCars(HostBlock & hb) {
    
    TheL1Data::CarIdxRB & crbi = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMM2COMP];
    TheL1Data::CarIdxRB & crbo = theACacheBlockL1Data.getCarIdxs(0).mTheIdxs[TheL1Data::CarIdxs::COMP2COMM];

    MFM_API_ASSERT_NONNULL(mACBL1Control);
    ACacheBlockL1Control & acl1 = *mACBL1Control;
    ACacheBlock * curacb = acl1.mCurrentACacheBlock; // if any

    /*
    HBPTAG(privUpCa,curacb);
    HBPX(!crbi.isEmpty());
    HBPX(!crbo.isFull());
    HBPX(acl1.readyToClose());
    */

    /** we are the only one that can falsify any of these conditions,
        if they are currently true, so we don't have to lock until we
        know we want to do so. right??
     */

    if (curacb != 0 &&          // have a car and
        !crbi.isEmpty() &&      // more empty cars are available and
        !crbo.isFull() &&       // more full cars are shippable and
        acl1.readyToClose()) {  // current car is ready to go
      bool got;

      LOGPX(curacb);
      LOGPX(!crbi.isEmpty());
      LOGPX(!crbo.isFull());
      LOGPX(acl1.readyToClose());

    HBPX(!crbi.isEmpty());
    HBPX(!crbo.isFull());
    HBPX(acl1.readyToClose());
      

      { // SHIPPING
        ACacheBlock & olb = *curacb;
        ACacheBlockPayload & opay = olb.payload();
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        if (opay.getCurrentPayloadSize() < 4)
          LOGPTAG(SHIPFRMS,acl1.mCurrentCarIndex);
        else
          LOGPTAG(SHIPFRML,acl1.mCurrentCarIndex);
        HBPTAG(SHIPH,opay.getCurrentPayloadSize());
        got = crbo.add(acl1.mCurrentCarIndex); // hand control back to comm
        HBPTAG(ACBShipi,acl1.mCurrentCarIndex);
        HBASSERT_EQ(got,true);
      }
      // RECEIVING
      acl1.setupNewCar(crbi);                                   
    } else if (curacb == 0 && !crbi.isEmpty()) { // ready to init?
      HBMARK;
      acl1.setupNewCar(crbi) ;
      //      HBPTAG(r2init,&crbi);
      HBPTAG(acl1,acl1.mCurrentCarIndex);
    } else {
      // else not ready for anything
      //HBNOTE("uncovered?");
    }

    return 0;
  }

  int ACacheBlockPrivateControl::step(HostBlock & hb) {
    HBXX(mDLGridList);
    HBXX(mACBL1Control);
    MFM_API_ASSERT_NONNULL(mDLGridList);
    MFM_API_ASSERT_NONNULL(mACBL1Control);
    HBNOTE(ACBS11);
    if (true) {
      static u32 spin = 0u;
      if ((spin++ & 0xffff) == 0) {
        LOGPTAG(ACBPCstep,spin>>16);
        LOGPTAG(DGLLen,mDLGridList->getLength());
        LOGPX((u32) mState);
      }
    }
    HBNOTE(ACBS12);
    updateCars(hb);
    HBNOTE(ACBS13);
    tryToSendFrame(hb);
    HBNOTE(ACBS14);
    return 0;
  }

  ACacheBlock & ACacheBlockPrivateControl::getCurrentACBOrDie() {
    MFM_API_ASSERT_NONNULL(mACBL1Control);
    ACacheBlockL1Control & acl1 = *mACBL1Control;
    MFM_API_ASSERT_NONNULL(acl1.mCurrentACacheBlock);
    ACacheBlock & acb = *acl1.mCurrentACacheBlock;
    return acb;
  }

  void ACacheBlockPrivateControl::tryToSendFrame(HostBlock & hb) {
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
          LOGPTAG(2PACK,mFramesSent);
          HBPTAG(2PACK,mFramesSent);
          mNextFrameBogoMS += BOGOMS_PER_FRAME;
          if ((mFramesSent%100) == 0) LOGPTAG(WAITSENT,mFramesSent);
          mARsToSend = dl.getLength();
          mState = State::PACK;
        }
      }
      break;

    case State::WAIT:
      {
        if (mNextFrameBogoMS <= millisElapsed()) {
          LOGPTAG(2NEXT,mNextFrameBogoMS);
          HBPTAG(2NEXT,mNextFrameBogoMS);

          // ACCESS PAY
          ACacheBlock &acb = getCurrentACBOrDie();

          // SET RCLOSE
          acb.payload().setRCloseFlag();

          // WAIT TIL SHIPT? how?
          mState = State::NEXT;
        }
      }
      break;

    case State::PACK:
      {
        //        LOGPTAG(PACKI,mARsToSend);
        if (mNextFrameBogoMS <= millisElapsed()) {
          LOGPTAG(EOFTM,mFramesSent);
          mState = State::WAIT; // ran out of frame time
        }
        else if (false /*mARsToSend == 0*/) {
          // XXX JUST stay in PACK?
          // RUN END OF FRAME CODE
          //          if ((mFramesSent % 100) == 0)
          //            LOGPTAG(NEEDSENDEOFC,mFramesSent);
          mState = State::WAIT; // nothing else to send
        } else {

          // PACK SOME BYTES
          U8C arc;
          // CHECK IF HAVE ROOM AND AN OBJECT
          if (false) {
            u32 n = sizeof(AtomReport);
            LOGPX(n);
            LOGPX(l1dLZBytesIn.mFirstFreeIdx);
            LOGPX(l1dLZBytesIn.mFirstUsedIdx);
            bool has = l1dLZBytesIn.mFirstFreeIdx - l1dLZBytesIn.mFirstUsedIdx < l1dLZBytesIn.RING_BUFFER_SIZE - n;
            LOGPX(has);
            LOGPX(l1dLZBytesIn.hasRoomForNMore(n));
          }
          //          LOGPX(mARIO.canPutObj());
          if (l1dLZBytesIn.hasRoomForNMore(sizeof(AtomReport)) &&
              mARIO.canPutObj()) {
            AtomReport ar;
            if (dl.popBackC(arc)) {
              LOGPTAG(RUM4NMO,l1dLZBytesIn.mFirstUsedIdx);
              ar.mCoord = U16C(arc.x,arc.y);
              T6Grid & t6g = theT6Grid[0];
              ar.mAtom = t6g.getAtom(ar.mCoord);
              //              LOGPTAG(GACK@,&t6g);
              LOGPTAG(PACKR,arc);
              LOGATOM(ar.mAtom);
              mARIO.putObj(ar);
              
              u8 byte;
              //              LOGPTAG(BP,&byte);
              while (mARIO.tryGetByte(byte)) {
                SNAP(10,LOGXTAG(B,(u32) byte));
                u32 spin = 0;
                while (!l1dLZBytesIn.add(byte)) { // "CAN'T BE FALSE"
                  if ((spin++ % (1u<<15))== 0)
                    LOGPTAG(ACBSPIN,spin>>15);
                }
              }
            } //else LOGMARK;
          } else {
            LOGPTAG(PAKOUT,arc);
          }
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
      HBNOTE("ACBT0ARO");

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {

      LOGMARK;
      HBNOTE(NCGO!);
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
  __attribute__((section(".rodata_fp_table_t0")))
  HTFuncPtr ACacheBlockEPPtrT0 = &manageACacheBlockT0;

  ////////
  static RCFlag manageACacheBlockNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      HBNOTE("MACBARO");
      LOGMARK;

      theACacheBlockL1Data.reset();     // zero all

      ACacheBlockL1Control & acbl1 = theACacheBlockL1Control;
      acbl1.init();             // init public state

      auto & theACacheBlockStgs = theACacheBlockL1Data.mTheTCStorages; // compiler knows the foggn array size

      ///// BIRTH

      // Set up cars
      for (u32 i = 0; i < sizeof(theACacheBlockStgs)/sizeof(theACacheBlockStgs[0]); ++i) {
        ACacheBlockStg & acbs = theACacheBlockStgs[i];
        HBPTAG(carsACBS,i);
        for (u32 c = 0; c < acbs.getCarCount(); ++c) {
          ACacheBlock & acb = acbs.getTC(c);
          acb.init();
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
    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      LOGMARK;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      //// LIFE
      //      SNAP(5,HBMARK);
      myACacheBlockEPNC.updateOps();

    } else LOGPTAG(unknown htoc,htoc);
    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr acbEPPtr = &manageACacheBlockNC;

  extern int myInitT1() ;
  extern int myLiveT1(HostBlock & hb) ;

  ////////
  TEFResult TaskEpochFunction_HACT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    switch (hei) {
    case HE_GROW0: //case HE_BORN0:
      HBNOTE(iHACT);
      if (hartnum == HARTNUM_T0) {
        manageACacheBlockT0(HTOC_INIT);
      } else if (hartnum == HARTNUM_T1) {
        myInitT1();
      } else if (hartnum == HARTNUM_NC) {
        manageACacheBlockNC(HTOC_INIT);
      }
      HBMARK;
      break;

    case HE_GROW1: //case HE_BORN1:
      HBNOTE(openHACT);
      if (hartnum == HARTNUM_T0) {
        manageACacheBlockT0(HTOC_OPEN);
      } else if (hartnum == HARTNUM_T1) {
        HBNOTE("T1 nongo");
      } else if (hartnum == HARTNUM_NC) {
        manageACacheBlockNC(HTOC_OPEN);
      }
      HBMARK;
      break;

    default:
      FAIL(UNREACHABLE_CODE);
    }
    return TEFR_CONTINUE;
  }

}
