/* -*- C++ -*- */

namespace MFM {

  template<class SUBEP, class SUBTC>
  void EP<SUBEP,SUBTC>::initEP(BlockCode bc, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn) {
    reset();
    HBMARK;

    mLockPtr = &lock;           // set up the lock
    AtomicScopeLock guard(getPlatformLock()); // then take it

    mDestBlockCode = bc;
    mDestBlockCodeIndex = U8_MAX;
    mCarCount = carCount;

    mIsIn = isIn;
    mCarsStartIn = carsIn;

    // here/gone inits identical on in and out:
    mOldestHere = U8_MAX;
    mHereCount = 0u;
    mOldestGone = 0u;
    mGoneCount = mCarCount;
    this->setFastEPState(EPState::INITTED);
    HBMARK;
  }

  template<class SUBEP, class SUBTC>
  bool EP<SUBEP,SUBTC>::updateOps() {
    AtomicScopeLock guard(getPlatformLock());
    
    bool ret = false;
    SNAP(2,HBMARK);
    /// TRY RECEIVING ARRIVALS
    while (mGoneCount > 0) {    // if we have gone cars

      SUBTC * carp = getCarPtr(mOldestGone); // oldest departed next to return
      MFM_API_ASSERT_NONNULL(carp);

      SUBTC & car = *carp;
      if (!car.isComplete()) break;

      TCState cs = car.getTCState();
      if (!isArriving(cs)) break;
      
      // Welcome! Let's get you set up here.
      TCOpsData & data = getOpsData(mOldestGone);
      data.mArrivalTime = millisElapsed();

      if (recvTC(car, mOldestGone)) {
        // successful recvTC MEANS:
        //  - one less gone car
        //  - one more here car
        //  - if this is the first here car,
        //    it is also the oldest here car
        HBPVAL(mOldestGone);

        if (mHereCount == 0u) mOldestHere = mOldestGone;
        ++mHereCount;
    
        mOldestGone = incrementIndex(mOldestGone);
        mGoneCount--;
      }
      else {
        break;               // need to block the line b/c we're somehow unready to receive you
      }
    }

    /// TRY SHIPPING DEPARTURES
    while (mHereCount > 0u) { // if we have cars here
      MFM_API_ASSERT(mHereCount <= mCarCount,ARRAY_INDEX_OUT_OF_BOUNDS);

      SNAP(2,HBMARK);
      SUBTC * carp = getClosedTCPtrIfAny();
      if (!carp) break; // nothing ready to go
      SUBTC & car = *carp;
      HBPVAL(mHereCount);

      MFM_API_ASSERT(car.isComplete(),ILLEGAL_STATE);
      TCState cs = car.getTCState();
      HBPVAL(cs);

      // ADVANCE CLOSED TO DEPARTING
      if (cs == TCState::CLOSED) {
        cs = departingState();
        HBPVAL(cs);
        car.setDepartingTC(cs);
      }

      HBPVAL(cs);
      if (!isDeparting(cs)) break; // (still) not ready to go

      // Bye now, come back soon!
      TCOpsData & data = getOpsData(mOldestHere);
      data.mDepartureTime = millisElapsed();

      HBPVAL(data.mDepartureTime);

      if (shipTC(car,mOldestHere)) {        // SHIPT!

        if (mGoneCount == 0u) mOldestGone = mOldestHere;
        mGoneCount++;
        HBPVAL(mGoneCount);
      
        mOldestHere = incrementIndex(mOldestHere);
        mHereCount--;
        HBPVAL(mHereCount);
      } else
        break;                  // try again later.
    }
    /// AND PROMENADE

    // anything more to do now? eventually: bust jams with timeouts, .. ? 

    return ret;
  }

}
