#include "P2PEWElevatorPlatform.h"
#include "T6ElevatorTransport.h"
#include "FastT0.h" // for millisElapsed

#include "FastLocal.h" // for fAll

namespace MFM {
  extern HostBlock theHostBlock;

  P2PEWElevatorPlatform::P2PEWElevatorPlatform()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  void P2PEWElevatorPlatform::initCars(EWCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
    AtomicScopeLock guard(getPlatformLock());
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(EWCar));
    {
      static u8 once;
      if (once++ > 0u) 
        FAIL(ILLEGAL_STATE);
    }
  }

  bool P2PEWElevatorPlatform::sendCar() {
    FAIL(INCOMPLETE_CODE);
#if 0    
    for (u32 tries = 0u; tries < mCarCount; ++tries) {
      EWCarStorage::EWCar * lcp = getCurrentCarIfAny();
      if (!lcp) FAIL(INCOMPLETE_CODE);
      if (lcp->getCarState() != CarState::OPEN) return false;
      EWBlock & lb = lcp->getContent();
      u32 room = lb.spaceRemaining();
      if (room > 0u) { // room for one more
        lb.addByte(byte);
        return true;
      }
      // current car is fully packed, so close it
      lcp->setCarState(CarState::CLOSED,CarType::STANDARD); 
      advanceToNextCar();       // and hope for rooom in the next one
    }
#endif
    return false; // we're blown.
  }

  P2PEWElevatorPlatform::EWCar * P2PEWElevatorPlatform::getCurrentCarIfAny() {
    if (mCurrentCarIdx >= mCarCount) return 0;
    return &mCars[mCurrentCarIdx];
  }

  bool P2PEWElevatorPlatform::update(T6ElevatorTransport & et) {

    AtomicScopeLock guard(getPlatformLock());

    {
      static u32 once = 0;
      if (once % 1000 == 0) {
        et.notice("%d P2PEWP.upd call %d/%d\n",++once,mCurrentCarIdx,mCarCount);
      }
    }

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      EWCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all EW cars host side
        // host: INBOUND_DEPARTED means just arrive
        // cross: INBOUND_DEPARTED means already gone
        //et.notice("EWINIT %d of %d\n",c,mCarCount);
        car.setCarState(CarState::INBOUND_DEPARTED, CarType::STANDARD);
        continue;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        CarSig hdr = car.getHeader(); 
        if (hdr.mCarState != CarState::OUTBOUND_DEPARTED) // which should only show as OUTBOUND_DEPARTED?
          et.notice("(%d,%d)BADCAR: m%02x n%d s%d t%d\n",
                    fAll.mPos.x,fAll.mPos.y,
                    hdr.mCarMagic,
                    hdr.mCarNonce,
                    hdr.mCarState,
                    hdr.mCarType);
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
      static u32 once = 0u;
      if (once < 3u) {
        et.notice("P2PEWARR 0x%08x %d @ %u\n",&car,c,carmeta.mArrivalTime);
        ++once;
      }
    }
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        {
          static u8 once;
          if (once < 5) {
            et.notice("EWLOPN(%d)\n",c);
            ++once;
          }
        }

        if (car.readyToClose(carmeta,millisElapsed())) {
        {
          static u8 once;
          if (once < 5) {
            et.notice("EWCLSR(%d)\n",c);
            ++once;
          }
        }

          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        ret = et.ship(car, c); // success advances to departing state
        {
          static u8 once;
          if (once < 5) {
            et.notice("PEWSHIP(%d)->%d\n",c,ret);
            ++once;
          }
        }
        break;
      }
    }
    return ret;
  }
}

