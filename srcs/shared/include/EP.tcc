/* -*- C++ -*- */

namespace MFM {

  template<class SUBEP, class SUBTC>
  void EP<SUBEP,SUBTC>::initEP(EndPointAddress srcEPA, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn) {
    reset();
    HBPTAG(initEP,this->getName());

    mLockPtr = &lock;           // set up the lock
    AtomicScopeLock guard(getPlatformLock()); // then take it

    mSrcEPA = srcEPA;
    mDestEPA = {BlockCode::BC_RSRV_ILL, U8_MAX};
    mCarCount = carCount;

    mIsIn = isIn;
    mCarsStartIn = carsIn;

    // here/gone inits identical on in and out:
    mOldestHere = U8_MAX;
    mHereCount = 0u;
    mOldestGone = 0u;
    mGoneCount = mCarCount;
    this->setFastEPState(EPState::INITTED);
    HBPTAG(sEPA,getNameFromBlockCode(mSrcEPA.mBlockCode));
    HBPTAG(sEPA,mSrcEPA.mBlockCodeIndex);
    //HBMARK;
  }

  template<class SUBEP, class SUBTC>
  bool EP<SUBEP,SUBTC>::updateOps() {
    //HBPTAG(uOps,this);
    AtomicScopeLock guard(getPlatformLock());
    
    bool ret = false;
    /// TRY RECEIVING ARRIVALS
    while (mGoneCount > 0) {    // if we have gone cars
      if (false) { static u32 lastgone;
        if (lastgone != mGoneCount) {
          HBPTAG(NUgonect,mGoneCount);
          lastgone = mGoneCount;
        }
      }

      SUBTC * carp = getCarPtr(mOldestGone); // oldest departed next to return
      MFM_API_ASSERT_NONNULL(carp);

      SUBTC & car = *carp;
      if (!car.isComplete()) {
        SNAP(10,HBPTAG(gonBLK,carp));
        break;
      }

      //      HBPTAG(EPgonect,mGoneCount);
      //      HBPTAG(EPoldidx,mOldestGone);

      TCState cs = car.getTCState();
      if (!isArriving(cs)) {
        //HBPTAG(EP,this->getName());
        //HBPTAG(notAR,getCarStateName(cs));
        //HBPTAG(wein,mIsIn);
        break;
      }

      // Welcome! Let's get you set up here.
      TCOpsData & data = getOpsData(mOldestGone);
      data.mArrivalTime = millisElapsed();

      //      HBPTAG(EPARRV,this->getName());
      //      HBPTAG(EPARRL,(u32) mSrcEPA.mBlockCodeIndex);
      if (recvTC(car, mOldestGone)) {
        // successful recvTC MEANS:
        //  - one less gone car
        //  - one more here car
        //  - if this is the first here car,
        //    it is also the oldest here car
        //HBPVAL(mOldestGone);

        if (mHereCount == 0u) mOldestHere = mOldestGone;
        ++mHereCount;
    
        mOldestGone = incrementIndex(mOldestGone);
        mGoneCount--;
        ret = true;
        //        HBPTAG(EPRCVD,this->getName());
      }
      else {
        break;               // need to block the line b/c we're somehow unready to receive you
      }
    }

    /// TRY SHIPPING DEPARTURES
    while (mHereCount > 0u) { // if we have cars here
      if (false) { static u32 lasthere;
        if (lasthere != mHereCount) {
          HBPTAG(NUherect,mHereCount);
          lasthere = mHereCount;
        }
      }

      MFM_API_ASSERT(mHereCount <= mCarCount,ARRAY_INDEX_OUT_OF_BOUNDS);

      SUBTC * carp = getClosedTCPtrIfAny();
      if (!carp) break; // nothing ready to go
      SUBTC & car = *carp;

      MFM_API_ASSERT(car.isComplete(),ILLEGAL_STATE);
      TCState cs = car.getTCState();
      //      HBPTAG(hercs,cs);

      // ADVANCE CLOSED TO DEPARTING
      if (cs == TCState::CLOSED) {
        cs = departingState();
        car.setDepartingTC(cs);
        //        HBPTAG(advdep,car.getTCState());
      }

      //HBPVAL(cs);
      if (!isDeparting(cs)) break; // (still) not ready to go

      // Bye now, come back soon!
      TCOpsData & data = getOpsData(mOldestHere);
      data.mDepartureTime = millisElapsed();

      //      HBPTAG(dptim,data.mDepartureTime);

      if (shipTC(car,mOldestHere)) {        // SHIPT!
        // DEBUG: DELIBERATELY BREAK SHIPT CARS
        // DEBUG: TO READ AS INCOMPLETE UNTIL THEY RETURN
        car.getHeader().mTCMMagic = 0;

        if (mGoneCount == 0u) mOldestGone = mOldestHere;
        mGoneCount++;
        //        HBPTAG(goneCt,mGoneCount);
      
        mOldestHere = incrementIndex(mOldestHere);
        mHereCount--;
        //HBPVAL(mHereCount);
        ret = true;
      } else
        break;                  // try again later.
    }
    /// AND PROMENADE

    // anything more to do now? eventually: bust jams with timeouts, .. ? 

    return ret;
  }

}
