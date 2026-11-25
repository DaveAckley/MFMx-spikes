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

  static constexpr u32 EARLY_FOLDS = 16u; // prefix length for the .zn early-sum check

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
    // A full car (data space filled, trailer appended: +4) is not a jump.
    bool fulltrailer = (wasused == (u32) mExpectLen + 4u) &&
      ((u32) mExpectLen == ACacheBlockPayload::ACBP_DATA_SIZE);
    if (wasused != mExpectLen && !fulltrailer) {  // length moved under us
      ++mLenJumps;
      if (mLenJumpsThisCar < 3u) {
        ++mLenJumpsThisCar;     // .zl:found:expected:flags:car:ms since setup
        // .zl:found:expected:flags:car:ms since setup:ms since this
        //   car's last recvTC:recvTC count for this car
        char lb[80];
        u32 ci = ((u32) mCurrentCarIndex) & 7u;
        snprintf(lb,sizeof(lb),"%x:%x:%x:%x:%x:%x:%x",wasused,
                 (u32) mExpectLen,pay.getCurrentFlags(),(u32) mCurrentCarIndex,
                 millisElapsed() - mBaseTicks,
                 millisElapsed() - mRecvMs[ci],mRecvN[ci]);
        LOGPTAG(.zl,lb); HBPTAG(.zl,lb);
      }
    }
    bool ret = pay.addByte(byte);
    if (ret) mExpectLen = (u16) pay.getCurrentLength();
    if (ret) {
      ++mCompressedBytesPacked;
      ACacheBlockPayload::fletcherStep(mShadowA,mShadowB,byte);
      if (++mShadowN == EARLY_FOLDS)
        mShadowEarly = (u16) ((mShadowB << 8) | mShadowA);
      if (wasused < sizeof(mShadowCar)) mShadowCar[wasused] = byte;
    }
    {
      if (ret /*true wasused < 3 || wasused > 990-3*/) {
        u32 nowused = pay.getCurrentLength();
        u32 idx = mCompressedBytesPacked-1;
        char buf[40];
        snprintf(buf,40,"#%u @%u:%u 0x%02x",idx,wasused,nowused,byte);
        if (!ret) SNAP(2,LOGPTAG(sWBC>,buf));
        else if ((idx % 986) > 976 || (idx % 986) <  20)
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
    mShadowA = mShadowB = 0u;   // fresh car, fresh shadow sum
    mShadowN = mShadowEarly = 0u;
    mExpectLen = 0u;            // reset() above zeroed the length
    mLenJumpsThisCar = 0u;
    if (mPendingSOF) {          // first car of a restarted stream (we hold mLock)
      npay.mFlagsAndLen |= ACacheBlockPayload::ACBP_FLAG_IS_SOF |
        (mSOFEpoch ? ACacheBlockPayload::ACBP_FLAG_EPOCH : 0u);
      mPendingSOF = 0u;
    }
  }

  /* The car does not hold what the sink accepted (the shadow sum said so).
     Compare byte for byte (the trailer is already appended: the data bytes
     only) and log: .zx = seq, bytes that differ, first differing offset, then
     up to five (offset:meant:found) triples.  Both LOG and HB marks. */
  void ACacheBlockL1Control::explainShadow(const ACacheBlockPayload & pay, u16 seq) {
    u32 dlen = pay.getDataLength();
    u32 ndiff = 0u, first = 0u;
    char buf[60];
    for (u32 i = 0u; i < dlen && i < sizeof(mShadowCar); ++i) {
      if (pay.mCData[i] == mShadowCar[i]) continue;
      if (ndiff++ == 0u) first = i;
    }
    snprintf(buf,sizeof(buf),"%x:%x:%x:%x",(u32) seq,dlen,ndiff,first);
    LOGPTAG(.zx,buf); HBPTAG(.zx,buf);
    u32 shown = 0u;
    for (u32 i = 0u; i < dlen && i < sizeof(mShadowCar) && shown < 5u; ++i) {
      if (pay.mCData[i] == mShadowCar[i]) continue;
      snprintf(buf,sizeof(buf),"%x:%x:%x",i,(u32) mShadowCar[i],(u32) pay.mCData[i]);
      LOGPTAG(.zy,buf); HBPTAG(.zy,buf);
      ++shown;
    }
  }

  //// Stream restart (see EP_ACacheBlock.h)
  void ACacheBlockL1Control::latchHostFeedback(const ACacheBlockPayload & pay) {
    AtomicScopeLock guard(mLock);
    u16 f = (u16) pay.mFlagsAndLen;
    mHostReq = (f & ACacheBlockPayload::ACBP_FLAG_RESTART_REQ) ? 1u : 0u;
    mHostReqEpoch = (f & ACacheBlockPayload::ACBP_FLAG_EPOCH) ? 1u : 0u;
    if (!mHostReq) {            // no restart: the bytes are refresh requests
      for (u32 i = 0u; i < ACacheBlockPayload::REFRESH_SLOTS; ++i) {
        U8C c;
        if (!pay.getRefreshSlot(i,c)) continue;
        if (!DLGridList::isValidC(c)) continue;
        ++mRefreshAsked;
        if (mRefreshN >= REFRESH_QLEN) { ++mRefreshFull; continue; }
        mRefreshC[mRefreshN++] = c;
      }
    }
    if (mHostReq) {
      ACBHostFeedback fb = pay.getFeedback();
      mHostSpin = fb.mExpectedSpin;
      mHostSeq = fb.mPacketSeq;
      mHostConsec = fb.mConsecInvalid;
    }
  }

  bool ACacheBlockL1Control::closeIfData() {
    AtomicScopeLock guard(mLock);   // T1 adds bytes to this car under mLock
    if (!mCurrentACacheBlock) return false;
    ACacheBlockPayload & pay = mCurrentACacheBlock->payload();
    if (pay.getCurrentLength() == 0u || pay.checkRCloseFlag()) return false;
    pay.setRCloseFlag();
    return true;
  }

  bool ACacheBlockL1Control::takeRefreshSite(U8C & c) {
    memoryFence();
    if (mRefreshN == 0u) return false;     // common case: no lock
    AtomicScopeLock guard(mLock);
    if (mRefreshN == 0u) return false;
    --mRefreshN;
    c = mRefreshC[mRefreshN];
    return true;
  }

  bool ACacheBlockL1Control::restartWanted() {
    // T0 asks every step: the common answer (no) must not take the lock.  A
    // racy read of these bytes is harmless: a stale yes/no is corrected by the
    // next step, and a yes is rechecked under the lock.
    memoryFence();
    if (!mHostReq || mHostReqEpoch == mHandledEpoch) return false;
    AtomicScopeLock guard(mLock);
    return mHostReq && mHostReqEpoch != mHandledEpoch;
  }

  void ACacheBlockL1Control::beginDrain() {
    AtomicScopeLock guard(mLock);
    mSOFEpoch = mHostReqEpoch;  // the request we are answering
    memoryFence();
    mDraining = 1u;
  }

  bool ACacheBlockL1Control::isDraining() const {
    memoryFence();
    return mDraining != 0u;
  }

  void ACacheBlockL1Control::finishStream() {
    AtomicScopeLock guard(mLock);
    mPendingSOF = 1u;           // set before the car can ship
    if (mCurrentACacheBlock) {  // the encoder's flush is in it: end it now
      ACacheBlockPayload & pay = mCurrentACacheBlock->payload();
      pay.mFlagsAndLen |= ACacheBlockPayload::ACBP_FLAG_IS_EOF |
        ACacheBlockPayload::ACBP_FLAG_CLOSABLE;
    }
  }

  bool ACacheBlockL1Control::sofPending() {
    AtomicScopeLock guard(mLock);
    return mPendingSOF != 0u;
  }

  void ACacheBlockL1Control::streamRestarted() {
    AtomicScopeLock guard(mLock);
    mHandledEpoch = mSOFEpoch;
    ++mRestartsDone;
    logRestart('d',0u);
    memoryFence();
    mDraining = 0u;             // T0 may resume
  }

  // .zq = restart asked for (T0), .zd = restart done (T1): epoch, bytes
  // compressed so far, our spin, the host's expected spin, packets, bad run.
  void ACacheBlockL1Control::logRestart(const char what, u32 spinner) {
    char buf[60];
    snprintf(buf,sizeof(buf),"%x:%x:%x:%x:%x:%x:%x",
             (u32) mSOFEpoch,mCompressedBytesPacked,spinner,
             (u32) mHostSpin,(u32) mHostSeq,(u32) mHostConsec,mRestartsDone);
    if (what == 'q') {
      LOGPTAG(.zq,buf); HBPTAG(.zq,buf);
      char sb[40];              // cars that failed the seal check : stale reads
      snprintf(sb,sizeof(sb),"%x:%x:%x:%x",mShadowBad,mShadowStale,
               mLenJumps,mRecvCurrent);
      LOGPTAG(.zs,sb); HBPTAG(.zs,sb);
    }
    else             { LOGPTAG(.zd,buf); HBPTAG(.zd,buf); }
  }

  void ACacheBlockEP::initACacheBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) {
    LOGXTAG(InLBEP,&l1data);
    this->initT6EP(srcEPA, isin, l1data);
  }
     
  bool ACacheBlockEP::recvTC(ACacheBlock & car, u8 carindex) {
    Super::L1Data::CarIdxRB & crbi = getCarIdxs().mTheIdxs[Super::L1Data::CarIdxs::COMM2COMP];
    //HBPTAG(ABRCV?,carindex);

    if (crbi.isFull()) return false; // bail if can't notify??

    HBPTAG(ABRCV!,carindex);
    theACacheBlockL1Control.latchHostFeedback(car.payload()); // BEFORE the wipe
    {                           // are we about to wipe the car T1 is filling?
      ACacheBlockL1Control & cl1 = theACacheBlockL1Control;
      if (cl1.mCurrentACacheBlock && carindex == cl1.mCurrentCarIndex) {
        ++cl1.mRecvCurrent;
        if (cl1.mRecvCurrent <= 5u) {   // .zr:car:state:flags
          char rb[40];
          snprintf(rb,sizeof(rb),"%x:%x:%x",(u32) carindex,
                   (u32) car.getTCState(),(u32) car.payload().mFlagsAndLen);
          LOGPTAG(.zr,rb); HBPTAG(.zr,rb);
        }
      }
    }
    {                           // when did this car last come back? (.zl)
      ACacheBlockL1Control & rl1 = theACacheBlockL1Control;
      u32 ri = ((u32) carindex) & 7u;
      rl1.mRecvMs[ri] = millisElapsed();
      ++rl1.mRecvN[ri];
    }
    car.openTC();               // open it up
    car.payload().init();       // clean it out
    // The wipe is a ~1KB memset of posted stores, but the index add below is a
    // single committed word that can become visible FIRST: T0 would then pop
    // the car, reset it and let T1 start filling it while the tail of the
    // wipe still lands (bytes zeroed or the length reset under T1's feet, seen
    // as .zn N > DLEN).  So: fence, then commit (store+reload) the first and
    // last bytes of the payload, then announce the car.
    {
      ACacheBlockPayload & wp = car.payload();
      u8 * wb = (u8 *) &wp;
      memoryFence();
      writeCommitL1<u8>(&wb[sizeof(wp) - 1],0u);
      writeCommitL1<u8>(&wb[0],0u);
      memoryFence();
    }
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

  //////// T0 MSTICK HART TASK
  FAST_LOCAL(ACacheBlockPrivateControl,myPACBControlT0,0);

  void ACacheBlockPrivateControl::init(ACacheBlockL1Control & acbl1, DLGridList & dll1) {
    ONE_PING_ONLY();
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    HBNOTE(acbpriv2pubinit);
    memset_s(this,'\0',sizeof(*this)); // init all
    mACBL1Control = &acbl1;
    mDLGridList= &dll1;
    mARIO.init(true);   // true: init for serialization (obj -> bytes)
    mEpochSent = NO_EPOCH_SENT; // the stream starts with a marker
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
        // The shadow sums were written by T1 under mLock; we read them here
        // with no lock.  Read once, fence (flush our non-coherent L0), read
        // again: a difference is a stale read, and the post-fence value is the
        // one to trust.
        u16 a0 = *(volatile u16 *) &acl1.mShadowA;
        u16 b0 = *(volatile u16 *) &acl1.mShadowB;
        memoryFence();
        u16 a1 = *(volatile u16 *) &acl1.mShadowA;
        u16 b1 = *(volatile u16 *) &acl1.mShadowB;
        if (a0 != a1 || b0 != b1) ++acl1.mShadowStale;
        // seq + shadow checksum, counted in the length
        if (!opay.sealTrailer(mPktSeq,a1,b1)) {
          {                     // the evidence: run sums before/after the fence,
            char wb[60];        // sum of the byte copy, sum of the car now
            u32 dl = opay.getDataLength();
            u16 arr = ACacheBlockPayload::checksum(acl1.mShadowCar,dl);
            u16 car = ACacheBlockPayload::checksum(opay.mCData,dl);
            // fold count vs length, and the sum after the first EARLY_FOLDS
            // folds vs the same prefix of the byte copy and of the car
            char nb[60];
            u32 pre = dl < EARLY_FOLDS ? dl : EARLY_FOLDS;
            snprintf(nb,sizeof(nb),"%x:%x:%x:%x:%x",(u32) acl1.mShadowN,dl,
                     (u32) acl1.mShadowEarly,
                     (u32) ACacheBlockPayload::checksum(acl1.mShadowCar,pre),
                     (u32) ACacheBlockPayload::checksum(opay.mCData,pre));
            LOGPTAG(.zn,nb); HBPTAG(.zn,nb);
            snprintf(wb,sizeof(wb),"%x:%x:%x:%x",(u32) ((b0<<8)|a0),
                     (u32) ((b1<<8)|a1),(u32) arr,(u32) car);
            LOGPTAG(.zw,wb); HBPTAG(.zw,wb);
          }
          ++acl1.mShadowBad;    // car differs from what the sink accepted
          LOGPTAG(SHADOWBAD,mPktSeq);
          HBPTAG(SHADOWBAD,mPktSeq);
          acl1.explainShadow(opay,mPktSeq);
        }
        ++mPktSeq;
        olb.closeTC(opay.getCurrentPayloadSize()); // close the car
        {                       // trailer and close must be in L1 before the
          u32 dl2 = opay.getCurrentLength();        // car is announced
          memoryFence();
          if (dl2 > 0u && dl2 <= sizeof(opay.mCData))
            writeCommitL1<u8>(&opay.mCData[dl2 - 1],opay.mCData[dl2 - 1]);
          memoryFence();
        }
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

  bool ACacheBlockPrivateControl::restartStep(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    MFM_API_ASSERT_NONNULL(mACBL1Control);
    ACacheBlockL1Control & acl1 = *mACBL1Control;
    if (mRestartPhase == 0u) {
      if (!acl1.restartWanted()) return false;     // the common case
      mRestartPhase = 1u;
      acl1.mSOFEpoch = acl1.mHostReqEpoch;         // for the log line
      acl1.logRestart('q',mARSpinner);
    }
    if (mRestartPhase == 1u) {
      // Finish the record in flight: only whole records go to the compressor,
      // so the stream ends on a record boundary.
      u8 byte;
      while (!l1dLZBytesIn.isFull() && mARIO.tryGetByte(byte)) {
        l1dLZBytesIn.add(byte);
        ++mUncompressedBytesPacked;
      }
      if (mARIO.canGetByte()) return true;         // ring full: T1 drains it, try again
      mARIO.discardObj();
      acl1.beginDrain();
      mRestartPhase = 2u;
      return true;
    }
    // phase 2: T1 ends the stream, flushes, has the SOF shipped, resets
    if (acl1.isDraining()) return true;
    mRestartPhase = 0u;
    mEpochSent = NO_EPOCH_SENT;                    // the host's decoder reset: say it again
    mState = State::WAIT;                          // fresh frame timing
    mNextFrameBogoMS = millisElapsed();
    return false;
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
    if (!restartStep(hb)) tryToSendFrame(hb);
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
    {                           // host-asked refreshes: report them next
      U8C rq;
      while (mACBL1Control->takeRefreshSite(rq)) {
        dl.pushFrontC(rq);
        if ((++mRefreshQueued % 64u) == 1u) LOGPTAG(.hr,mRefreshQueued);
      }
    }
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
          if ((mFramesSent%1000) == 1) LOGPTAG(WAITSENT,mFramesSent);
          mARsToSend = dl.getLength();
          mState = State::PACK;
        }
      }
      break;

    case State::WAIT:
      {
        // Check if we're go to suspend
        if (!theL1GridManagerControl.isEPSuspStatus() && // not already suspended
            theL1GridManagerControl.isEPSuspReq() &&     // but a request has been made
            theL1GridManagerControl.isEPSuspReqSeen() && // and applyEWT has seen it
            dl.getLength() == 0) { // and we have nothing more to send
          theL1GridManagerControl.setEPSuspStatus(true); // then suspend
          LOGPTAG(HERBO_SUSPACH,theL1GridManagerControl.isEPSuspStatus());
        } else if (false && theL1GridManagerControl.isEPSuspStatus() && // if already suspended
                   !theL1GridManagerControl.isEPSuspReq()) { // but no request is active
          theL1GridManagerControl.setEPSuspStatus(false);
          LOGPTAG(HERBO_NOSUSPACH,dl.getLength());
        }

        if (mNextFrameBogoMS <= millisElapsed()) {
          //          LOGPTAG(2NEXT,mNextFrameBogoMS);
          EACH(100,HBPTAG(2NEXT,mNextFrameBogoMS));

          // ACCESS PAY
          ACacheBlock &acb = getCurrentACBOrDie();

          // SET RCLOSE only when idle (see ACB_IDLE_CLOSE); a busy hub
          // fills its cars (the XXX: let's fill the foggencar)
          if (ACB_IDLE_CLOSE && dl.getLength() == 0u &&
              mACBL1Control->closeIfData()) {
            if ((++mIdleCloses % 256u) == 1u) LOGPTAG(.ic,mIdleCloses);
          }

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

            /// (1b) Epoch changed since the last marker? Say so, in order:
            /// every record after the marker was read in the new epoch.
            {
              const u32 epoch = theL1GridManagerControl.getReportEpoch();
              if (epoch != mEpochSent) {
                ar.mCoord = U16C(AtomReport::EPOCH_MARK_X,(u16) epoch);
                ar.mSpin1 = ar.mSpin2 = mARSpinner++;
                ar.mAtom = P4Atom::makeEmptyAtom();
                mARIO.putObj(ar);
                mEpochSent = epoch;
                if ((++mEpochMarkers % 16u) == 1u) LOGPTAG(.em,epoch);
                continue;
              }
            }

            /// (2) Does dl have more ARs to supply?
            U8C arc;
            if (dl.popBackC(arc)) {
              // Yes. Make report
              mDirty = 1u;
              ar.mAtom = t6g.getAtom(U16C(arc.x,arc.y));
              ar.mCoord = U16C(AtomReport::withStamp(arc.x,siteStampByte(arc,mEpochSent)),
                               arc.y);
              ar.mSpin1 = ar.mSpin2 = mARSpinner++;
              //EACH(1,{LOGPTAG(AC:,ar.mCoord);LOGATOM(ar.mAtom);});
              
              // Stash it (or die: We Just Cleared mARIO)
              mARIO.putObj(ar);
              continue;
            }
            /// (2a) List empty after reports: pad the stream so the encoder's
            /// held tail comes out (ACB_IDLE_PAD)
            if (ACB_IDLE_PAD) {
              if (mDirty && mPadLeft == 0u) {
                mDirty = 0u;
                mPadLeft = PAD_RECORDS;
                if ((++mPadsStarted % 256u) == 1u) LOGPTAG(.ip,mPadsStarted);
              }
              if (mPadLeft > 0u) {
                --mPadLeft;
                ar.mCoord = U16C(AtomReport::EPOCH_MARK_X,(u16) mEpochSent);
                ar.mSpin1 = ar.mSpin2 = mARSpinner++;
                ar.mAtom = P4Atom::makeEmptyAtom();
                mARIO.putObj(ar);
                continue;
              }
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
      myPACBControlT0.init(theACacheBlockL1Control,theDLGridList);
      acbl1.setFlags(acbl1.ACBL1_T0_INITTED);
      LOGXTAG(T0INT,(u32)acbl1.getFlags());

      ret = RC_0_SELF_UP;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      EACH(1'000,HBPTAG(ACBLIV,__EACHNUM__));
      //// LIVING
      HostBlock & hb = theHostBlock;
      ACacheBlockPrivateControl & privcH0 = myPACBControlT0;
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
      myPACBControlT0.step(hb);
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
