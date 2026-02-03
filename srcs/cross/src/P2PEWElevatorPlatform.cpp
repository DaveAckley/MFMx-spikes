#include "P2PEWElevatorPlatform.h"
#include "T6ElevatorTransport.h"
#include "CrossUtils.h"         // for memset_s
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
    MFM_API_ASSERT_NONNULL(stg);
    MFM_API_ASSERT_NONNULL(meta);
    AtomicScopeLock guard(getPlatformLock());
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarMetadata = meta;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(EWCar));
    memset_s(mCarMetadata,0u,count*sizeof(BaseCarMetadata));
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
    DIEWAY();
    if (mCurrentCarIdx >= mCarCount) return 0;
    DIEWAY();
    return &mCars[mCurrentCarIdx];
  }

  bool P2PEWElevatorPlatform::update(T6ElevatorTransport & et) {
    MFM_API_ASSERT_NONNULL(mCarMetadata);
    DIEWAY();

    AtomicScopeLock guard(getPlatformLock());

    DIEWAY();

    {
      static u32 once = 0;

      if (once++ % 1000 == 0) {
        //XXX_DEBUG_FUNC(__FILE__,__LINE__);
        //et.notice("%d P2PEWP.upd call %d/%d\n",once,mCurrentCarIdx,mCarCount);
      }
    }

    //XXX_DEBUG_FUNC(__FILE__,__LINE__);
    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {

      EWCar& car = mCars[c];
      BaseCarMetadata & carmeta = mCarMetadata[c];
      CarState cs = car.getCarState();
    DIEWAY();
      if (cs == CarState::UNUSED) {
        // start with all EW cars host side
        // host: INBOUND_DEPARTED means just arrive
        // cross: INBOUND_DEPARTED means already gone
        //et.notice("EWINIT %d of %d\n",c,mCarCount);
    DIEWAY();
        car.setCarState(CarState::INBOUND_DEPARTED, CarType::STANDARD);
    DIEWAY();
        continue;
      }
    DIEWAY();

      if (!car.isComplete()) {     // urgh we could be racing with inbound delivery or outbound shipping prep
    DIEWAY();
        CarSig hdr = car.getHeader(); 
        if (hdr.mCarState != CarState::OUTBOUND_DEPARTED &&  // delivery in progress, just wait?
            hdr.mCarState != CarState::INBOUND_DEPARTED) {   // shipping out in progress, just wait?
    DIEWAY();
          CarSig fut = car.getFooter();
          et.notice("(%d,%d)BADCAR#%d h:%02x%02x.%02x%02x f:%02x%02x.%02x%02x \n",
                    fAll.mPos.x,fAll.mPos.y, c,
                    hdr.mCarMagic, hdr.mCarNonce, hdr.mCarState, hdr.mCarType,
                    fut.mCarMagic, fut.mCarNonce, fut.mCarState, fut.mCarType);
    DIEWAY();
        }
    DIEWAY();
        continue;
      }
    DIEWAY();

      switch (cs) {

      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
    DIEWAY();
        if (isArriving(cs)) {   // You Have Arrived
    DIEWAY();
          carmeta.mArrivalTime = millisElapsed(); // note the time
    {
      static u32 once = 0u;
      if (once < 3u) {
        et.notice("P2PEWARR 0x%08x %d @ %u\n",&car,c,carmeta.mArrivalTime);
        ++once;
      }
    }
    DIEWAY();

          car.setCarState(CarState::OPEN,CarType::STANDARD); // now itz open for bidniss
    DIEWAY();
        } 
        // if isDeparting, wait for external developments
    DIEWAY();
        break;

      case CarState::OPEN:
    DIEWAY();
        {
          static u8 once;
          if (once < 5) {
            et.notice("EWLOPN(%d)\n",c);
            ++once;
          }
        }
    DIEWAY();

        if (car.readyToClose(carmeta,millisElapsed())) {
    DIEWAY();
        {
          static u8 once;
          if (once < 5) {
            et.notice("EWCLSR(%d)\n",c);
            ++once;
          }
        }
    DIEWAY();

          car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
    DIEWAY();

        }
        break;

      case CarState::CLOSED:
    DIEWAY();
        ret = et.ship(car, c); // success advances to departing state
    DIEWAY();
        break;
      }
    }

    DIEWAY();
    //XXX_DEBUG_FUNC(__FILE__,__LINE__);
    return ret;
  }
}

