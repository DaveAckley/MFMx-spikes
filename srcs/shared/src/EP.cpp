#include "EP.h"

namespace MFM {

  void EP::init(AtomicLock & lock, bool isIn, u32 carCount, bool carsIn) {
      reset();
      mLockPtr = &lock;
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
      bool carshere = (mIsIn == carsIn);
      if (carshere) {
        mOldestHere = 0u;
        mHereCount = mCarCount;
        mOldestGone = U8_MAX;
        mGoneCount = 0u;
      } else {
        mOldestHere = U8_MAX;
        mHereCount = 0u;
        mOldestGone = 0u;
        mGoneCount = mCarCount;
      }
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
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
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('r',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif

        TCBase * carp = getCarPtr(c);
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('n',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
        MFM_API_ASSERT_NONNULL(carp);
        TCBase & car = *carp;
        TCMarker & hdr = car.getHeader();
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('c',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif

        hdr.init(2u*c+1u);
        hdr.mTCMState = carshere ? arrivingState() : departingState();
        car.getFooter() = hdr;
      }
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(':',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
      
    }

  bool EP::updateOps() {

    AtomicScopeLock guard(getPlatformLock());
    
    bool ret = false;
    /// TRY RECEIVING ARRIVALS
    while (mGoneCount > 0) {    // if we have gone cars
      TCBase * carp = getCarPtr(mOldestGone); // oldest departed next to return
      MFM_API_ASSERT_NONNULL(carp);

      TCBase & car = *carp;
      if (!car.isComplete()) break;

      TCState cs = car.getTCState();
      if (!isArriving(cs)) break;

      // Welcome! Let's get you set up here.
      TCOpsData & data = getOpsData(mOldestGone);
      data.mArrivalTime = millisElapsed();

      if (recvTC(car, mOldestGone)) {
        mOldestGone = incrementIndex(mOldestGone);
        mGoneCount--;
      }
      else break;               // need to block the line b/c we're somehow unready to receive you
    }

    /// TRY SHIPPING DEPARTURES
    while (mHereCount > 0u) { // if we have cars here
      TCBase * carp = getCarPtr(mOldestHere); // oldest arrived next to depart
      MFM_API_ASSERT_NONNULL(carp);
      TCBase & car = *carp;

      if (!car.isComplete()) break; // might show as incomplete during loading
      TCState cs = car.getTCState();
      if (!isDeparting(cs)) break; // (still) not ready to go

      // Bye now, come back soon!
      TCOpsData & data = getOpsData(mOldestHere);
      data.mDepartureTime = millisElapsed();

      if (shipTC(car,mOldestHere)) {        // SHIPT!
        mOldestHere = incrementIndex(mOldestHere);
        mHereCount--;
      } else
        break;                  // try again later.
    }
    /// AND PROMENADE

    // anything more to do now? eventually: bust jams with timeouts, .. ? 

#if 0    
    for (u32 c = 0u; c < CAR_COUNT; ++c) {
      CAR_TYPE * carp = self().getCurrentCarIfAny();
      if (!carp) continue;
      u32 carnum = self().getCurrentCarIndex();
      CAR_TYPE& car = *carp;
      TCOpsData & cartms = getCurrentCarOps();
      TCState cs = car.getTCState();

      if (cs == TCState::UNUSED) {
        // start with all cars on T6
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrived
        car.setTCState(TCState::OUTBOUND_DEPARTED, TCType::STANDARD);

        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress

        if (false) self().logTo().print("incomplete car\n");
        continue;
      }

      switch (cs) {
      case TCState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case TCState::INBOUND_DEPARTED:
      case TCState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          cartms.mArrivalTime = millisElapsed(); // note the time
          TCSig sig = car.getHeader();
          if (sig.mTCSType == TCType::EMPTY) {
            car.reset();         // clean out whole content
          }
          car.setTCState(TCState::OPEN,TCType::STANDARD); // now its standard
        } 
        // if isDeparting, wait for external developments
        break;

      case TCState::OPEN:
        if (car.readyToClose(cartms,millisElapsed())) {
          if (car.isEmpty())
            car.setTCState(TCState::CLOSED,TCType::EMPTY); 
          else
            car.setTCState(TCState::CLOSED,TCType::STANDARD); // Please Buckle Up
        }
        break;

      case TCState::CLOSED:
        ret = ship(car, carnum); // success advances to departing state
        break;
      }
    }
#endif
    return ret;
  }

}
