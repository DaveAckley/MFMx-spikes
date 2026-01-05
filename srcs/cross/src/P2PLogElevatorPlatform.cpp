#include "P2PLogElevatorPlatform.h"
#include <string.h>
#include "Fail.h"
#include "FATAL.h"
#include "BaseCar.h"
#include "T6ElevatorTransport.h"
#include "FastT0.h" // for millisElapsed
#include "FastLocal.h" // for fAll

namespace MFM {
  P2PLogElevatorPlatform::P2PLogElevatorPlatform()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  void P2PLogElevatorPlatform::initCars(LogCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(LogCar));
    {
      static u8 once;
      if (once++ > 0u) 
        FAIL(ILLEGAL_STATE);
    }
  }

  bool P2PLogElevatorPlatform::sendByte(u8 byte) {
    for (u32 tries = 0u; tries < mCarCount; ++tries) {
      LogCarStorage::LogCar * lcp = getCurrentCarIfAny();
      if (!lcp) FAIL(INCOMPLETE_CODE);
      if (lcp->getCarState() != CarState::OPEN) return false;
      LogBlock & lb = lcp->getContent();
      BaseCarMetadata & carmeta = mCarMetadata[mCurrentCarIdx];
      if (lb.isEmpty())         // first customer!
        carmeta.mOccupiedTime = millisElapsed(); // note the time!

      u32 room = lb.spaceRemaining();
      if (room > 32u || byte != '\n') { // room for one more
        lb.addByte(byte);
        return true;
      }
      // current car is fully packed, so close it
      lcp->setCarState(CarState::CLOSED,CarType::STANDARD); 
      advanceToNextCar();       // and hope for rooom in the next one
    }
    return false; // we're blown.
  }

  P2PLogElevatorPlatform::LogCar * P2PLogElevatorPlatform::getCurrentCarIfAny() {
    if (mCurrentCarIdx >= mCarCount) return 0;
    return &mCars[mCurrentCarIdx];
  }

  bool P2PLogElevatorPlatform::update(T6ElevatorTransport & et) {

    AtomicScopeLock guard(getPlatformLock());

    extern HostBlock theHostBlock;
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::TRYING; 

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      LogCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all cars on T6
        // host: OUTBOUND_DEPARTED means already gone
        // cross: OUTBOUND_DEPARTED means just arrived
        car.setCarState(CarState::OUTBOUND_DEPARTED, CarType::STANDARD);
        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        if (false) et.notice("incomplete car\n");
        continue;
      }

      switch (cs) {
      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
          carmeta.mArrivalTime = millisElapsed(); // note the time
    {
      static bool once;
      if (!once) {
        if (false) et.notice("P2PLogEPton 0x%08x of %d\n",&car,mCarCount);
        once = true;
      }
    }


          CarSig sig = car.getHeader();
          if (sig.mCarType == CarType::EMPTY) {
            { 
              static u8 once;
              if (false && once < 5u) {
                CarSig sig = car.getHeader();
                et.notice("%d-SCS(%x,%x,%x,%x)\n",once,
                          sig.mCarMagic, sig.mCarNonce,
                          sig.mCarState, sig.mCarType);
                if (sig.mCarType == CarType::STANDARD) {
                  CarSig fut = car.getStandardFooter();
                  et.notice("FUT(%x,%x,%x,%x)=%d\n",
                            fut.mCarMagic, fut.mCarNonce,
                            fut.mCarState, fut.mCarType,car.isComplete());
                }
                ++once;
              }
            }
            car.getContent().reset();         // clean out whole content
          }
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now its standard
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        {
          static u8 once;
          if (false && once < 1) {
            et.notice("P2PLOGopen car%d=%u, %u, %u\n",
                      c,
                      carmeta.mArrivalTime,
                      carmeta.mOccupiedTime,
                      millisElapsed());
            ++once;
          }
        }
        
        if (car.readyToClose(carmeta,millisElapsed())) {
          if (car.isEmpty())
            car.setCarState(CarState::CLOSED,CarType::EMPTY); 
          else
            car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        ret = et.ship(car, c); // success advances to departing state
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
        break;
      }
    }

    
    return ret;
  }
}

