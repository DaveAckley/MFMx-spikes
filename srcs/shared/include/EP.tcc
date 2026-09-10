/* -*- C++ -*- */

namespace MFM {

  template<class SUBEP, class SUBTC>
  void EP<SUBEP,SUBTC>::initEP(EndPointAddress srcEPA, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn) {
    reset();
    LOGPTAG(initEP,this->getName());

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
    LOGPTAG(sEPA,getNameFromBlockCode(mSrcEPA.mBlockCode));
    LOGPTAG(sEPA,mSrcEPA.mBlockCodeIndex);
    //HBMARK;
  }

  template<class SUBEP, class SUBTC>
  bool EP<SUBEP,SUBTC>::updateOps() {
    bool isLogXX = (this->getName()[0] == 'A');
    constexpr u32 BS = 100;
    char buf[BS];
    EACH(10'000,HBPTAG(EPuo,this->report(BS,buf)));
    //EACH(10'000,LOGPTAG(EPuo,this->report(BS,buf)));
    AtomicScopeLock guard(getPlatformLock());
    
    bool ret = false;
    /// TRY RECEIVING ARRIVALS
    while (mGoneCount > 0) {    // if we have gone cars
      if (true) { static u32 lastgone;
        if (lastgone != mGoneCount) {
          EACH(10'000,{HBPTAG(#GC,this->getName());HBPX(__EACHNUM__);});
          lastgone = mGoneCount;
        }
      }

      SUBTC * carp = getCarPtr(mOldestGone); // oldest departed next to return
      MFM_API_ASSERT_NONNULL(carp);

      SUBTC & car = *carp;
      if (!car.isComplete()) {
        if (isLogXX) SNAP(5,HBPTAG(gonBLK,carp));
        break;
      }

      //HBPTAG(EPgonect,mGoneCount);
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

      EACH(1'000'000,HBPTAG(EPRV,this->getName()));
      //HBPTAG(EPRL,(u32) mSrcEPA.mBlockCodeIndex);
      //HBPTAG(EPARRC,(u32) car);

      if (recvTC(car, mOldestGone)) {
        ++mRecvTCCount;
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
        //HBPTAG(<>R,this->report(BS,buf));
        //if (this->getSrcBlockCode() != BC_INTERHUB)
        /*
        if (this->getSrcBlockCode() == BC_ACACHEBLOCK)
          EACH(1,LOGPTAG(<>R,this->report(BS,buf)));
        */
        ret = true;
        //        HBPTAG(EPRCVD,this->getName());
      }
      else {
        break;               // need to block the line b/c we're somehow unready to receive you
      }
    }

    if (isLogXX) EACH(100'000,HBPTAG(LOG,__EACHNUM__));

    /// TRY SHIPPING DEPARTURES
    while (mHereCount > 0u) { // if we have cars here
      if (isLogXX) { static u32 lasthere;
        if (lasthere != mHereCount) {
          //HBPTAG(NUherect,mHereCount);
          lasthere = mHereCount;
        }
      }

      MFM_API_ASSERT(mHereCount <= mCarCount,ARRAY_INDEX_OUT_OF_BOUNDS);

      SUBTC * carp = getClosedTCPtrIfAny();
      if (!carp) break; // nothing ready to go
      SUBTC & car = *carp;

      MFM_API_ASSERT(car.isComplete(),ILLEGAL_STATE);
      TCState cs = car.getTCState();
      if (isLogXX) HBPTAG(hercs,cs);

      // ADVANCE CLOSED TO DEPARTING
      if (cs == TCState::CLOSED) {
        //        LOGPTAG(2DEP,this->report(BS,buf));
        cs = departingState();
        car.setDepartingTC(cs);
        if (isLogXX) HBPTAG(advdep,car.getTCState());
      }

      if (isLogXX) HBPVAL(cs);
      if (!isDeparting(cs)) break; // (still) not ready to go

      // Bye now, come back soon!
      TCOpsData & data = getOpsData(mOldestHere);
      data.mDepartureTime = millisElapsed();

      if (isLogXX) HBPTAG(dptim,data.mDepartureTime);

      if (shipTC(car,mOldestHere)) {        // SHIPT!
        ++mShipTCCount;
        // DEBUG: DELIBERATELY BREAK SHIPT CARS
        // DEBUG: TO READ AS INCOMPLETE UNTIL THEY RETURN
        car.getHeader().mTCMMagic = 0;

        if (mGoneCount == 0u) mOldestGone = mOldestHere;
        mGoneCount++;
        if (isLogXX) HBPTAG(goneCt,mGoneCount);
      
        mOldestHere = incrementIndex(mOldestHere);
        mHereCount--;
        //HBPTAG(<>S,this->report(BS,buf));
        //if (this->getSrcBlockCode() != BC_INTERHUB)
        /*
        if (this->getSrcBlockCode() == BC_ACACHEBLOCK)
          EACH(1,LOGPTAG(<>S,this->report(BS,buf)));
        */
        ret = true;
      } else
        break;                  // try again later.
    }
    /// AND PROMENADE

    // anything more to do now? eventually: bust jams with timeouts, .. ? 

    return ret;
  }

#ifndef BUILD_HOST
  template<class SUBEP, class SUBTC>
  char * EP<SUBEP,SUBTC>::report(u32 size, char * buf) const {
    npf_snprintf(buf,size,"[%s:%s:%u] c%u h%uo%u g%uo%u S%lu R%lu fs%u",
                 this->getName(),
                 getAbbrFromBlockCode(this->mSrcEPA.mBlockCode),
                 this->mSrcEPA.mBlockCodeIndex,
                 this->mCarCount,
                 this->mHereCount,this->mOldestHere,
                 this->mGoneCount,this->mOldestGone,
                 this->mShipTCCount,
                 this->mRecvTCCount,
                 this->getFastEPState()
                 );
    return buf;
  }
#else  
  template<class SUBEP, class SUBTC>
  char * EP<SUBEP,SUBTC>::report(u32 size, char * buf) const {
    snprintf(size,buf,"HONK");
    return buf;
  }
#endif
}
