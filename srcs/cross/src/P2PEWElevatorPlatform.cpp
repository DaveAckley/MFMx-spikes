#include "P2PEWElevatorPlatform.h"
#include "T6ElevatorTransport.h"

namespace MFM {
  P2PEWElevatorPlatform::P2PEWElevatorPlatform()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  void P2PEWElevatorPlatform::initCars(EWCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) {
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
      static bool once;
      if (!once) {
        et.notice("P2PEWElevatorPlatform::update called\n");
        once = true;
      }
    }

    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      EWCar& car = mCars[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all EW cars host side
        car.setCarState(mIsIn ? CarState::OPEN : CarState::INBOUND_DEPARTED,
                        CarType::STANDARD);
        break;
      }

      if (!car.isComplete()) {     // should only be possible if delivery in progress
        et.notice("incomplete car\n");
        continue;
      }

      switch (cs) {
      case CarState::UNUSED:
        FAIL(ILLEGAL_STATE);
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) {   // You Have Arrived
    {
      static bool once;
      if (!once) {
        et.notice("P2PEWARR 0x%08x of %d\n",&car,mCarCount);
        once = true;
      }
    }


          CarSig sig = car.getHeader();
          if (sig.mCarType == CarType::EMPTY) {
            { 
              static u8 once;
              if (once < 5u) {
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
            if (once < 5) {
              et.notice("lbRTC(%d)\n",c);
              ++once;
            }
          }

        if (car.readyToClose()) {
          if (car.isEmpty())
            car.setCarState(CarState::CLOSED,CarType::EMPTY); 
          else
            car.setCarState(CarState::CLOSED,CarType::STANDARD); // Please Buckle Up
        }
        break;

      case CarState::CLOSED:
        ret = et.ship(car, c); // success advances to departing state
        break;
      }
    }
    return ret;
  }
}

