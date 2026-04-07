namespace MFM {

  template<class SUBEP, class SUBTC>
  void EP<SUBEP,SUBTC>::init(BlockCode bc, u8 blkIdx, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn) {
      reset();
      mLockPtr = &lock;
      mDestBlockCode = bc;
      mDestBlockCodeIndex = blkIdx;
#ifndef BUILD_HOST
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('e',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
      AtomicScopeLock guard(getPlatformLock());
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('p',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
      mCarCount = carCount;
      mIsIn = isIn;

      // here/gone inits identical on in and out:
      mOldestHere = U8_MAX;
      mHereCount = 0u;
      mOldestGone = 0u;
      mGoneCount = mCarCount;

#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('f',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
      for (u32 c = 0u; c < mCarCount; ++c) {
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('r',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif

        SUBTC * carp = getCarPtr(c);
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('n',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
        MFM_API_ASSERT_NONNULL(carp);
        SUBTC & car = *carp;
        TCMarker & hdr = car.getHeader();
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('c',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif

        hdr.init(2u*c+1u);

        bool carshere = mIsIn == carsIn;
        hdr.mTCMState = carshere ? arrivingState() : departingState();

#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;

        theHostBlock.addBytes('#','0'+c);
        theHostBlock.addBytes('C','0'+carshere);
        theHostBlock.addBytes('S','0'+mIsIn);
        theHostBlock.addBytes('T','0'+hdr.mTCMState);
        once++;
      }
    }
#endif

        car.getFooter() = hdr;
      }
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (true) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(':',hartChar(fAll.mHartNum));
        theHostBlock.packString(getName());
        once++;
      }
    }
#endif
      
  }

  template<class SUBEP, class SUBTC>
  bool EP<SUBEP,SUBTC>::updateOps() {
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (false) {
        extern HostBlock theHostBlock;
        theHostBlock.packString(getName());
        theHostBlock.addBytes('u',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
    AtomicScopeLock guard(getPlatformLock());
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (false) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('p',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
    
    bool ret = false;
    /// TRY RECEIVING ARRIVALS
    while (mGoneCount > 0) {    // if we have gone cars
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('G','0'+mGoneCount);
    }
#endif

      SUBTC * carp = getCarPtr(mOldestGone); // oldest departed next to return
      MFM_API_ASSERT_NONNULL(carp);

#ifndef BUILD_HOST      
  if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('c','0'+mGoneCount);
    }
#endif
      SUBTC & car = *carp;
      if (!car.isComplete()) break;

      TCState cs = car.getTCState();
#ifndef BUILD_HOST      
   if (false)  {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('a','0'+(u8) cs);
    }
#endif
      if (!isArriving(cs)) break;

#ifndef BUILD_HOST      
  if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('w','0'+mGoneCount);
    }
#endif

      // Welcome! Let's get you set up here.
      TCOpsData & data = getOpsData(mOldestGone);
      data.mArrivalTime = millisElapsed();

#ifndef BUILD_HOST      
      {
        extern HostBlock theHostBlock;
        //        theHostBlock.addBytes('R','0'+mGoneCount);
      }
#endif

      if (recvTC(car, mOldestGone)) {
        // successful recvTC MEANS:
        //  - one less gone car
        //  - one more here car
        //  - if this is the first here car,
        //    it is also the oldest here car
#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('-','0'+mGoneCount);
    }
#endif
        if (mHereCount == 0u) mOldestHere = mOldestGone;
        ++mHereCount;
    
        mOldestGone = incrementIndex(mOldestGone);
        mGoneCount--;
      }
      else {
#ifndef BUILD_HOST      
   if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('=','0'+mGoneCount);
    }
#endif

        break;               // need to block the line b/c we're somehow unready to receive you
      }
    }

    /// TRY SHIPPING DEPARTURES
    while (mHereCount > 0u) { // if we have cars here

#ifndef BUILD_HOST      
    {
      extern HostBlock theHostBlock;
      //      theHostBlock.addBytes('H','0'+mHereCount);
    }
#endif

      SUBTC * carp = getCarPtr(mOldestHere); // oldest arrived next to depart
      MFM_API_ASSERT_NONNULL(carp);
      SUBTC & car = *carp;

      if (!car.isComplete()) break; // might show as incomplete during loading
      TCState cs = car.getTCState();
#ifndef BUILD_HOST      
    {
      extern HostBlock theHostBlock;
      //      theHostBlock.addBytes('A','0'+(u8) cs);
    }
#endif

      // ADVANCE CLOSED TO DEPARTING
    if (cs == TCState::CLOSED) {
#ifndef BUILD_HOST      
   if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('C','0'+mOldestHere);
    }
#endif
      car.setTCStateOnly(departingState());
    }

    if (!isDeparting(cs)) break; // (still) not ready to go

#ifndef BUILD_HOST      
    if (false) {
      extern HostBlock theHostBlock;
      theHostBlock.addBytes('B','0'+mHereCount);
    }
#endif

      // Bye now, come back soon!
      TCOpsData & data = getOpsData(mOldestHere);
      data.mDepartureTime = millisElapsed();

      if (shipTC(car,mOldestHere)) {        // SHIPT!
        if (mGoneCount == 0u) mOldestGone = mOldestHere;
        mGoneCount++;

        mOldestHere = incrementIndex(mOldestHere);
        mHereCount--;
      } else
        break;                  // try again later.
    }
    /// AND PROMENADE

    // anything more to do now? eventually: bust jams with timeouts, .. ? 

    return ret;
  }

}
