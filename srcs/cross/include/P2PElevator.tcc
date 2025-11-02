/* -*- C++ -*- */
#include <string.h>
#include "Fail.h"
#include "FATAL.h"
#include "BaseCar.h"

namespace MFM {
  template <class CAR,class ELEVATORTRANSPORT>
  P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::P2PElevatorPlatform()
    : mRemoteBaseAddress(0u)
    , mCars(0)
    , mCarCount(0u)
    , mCurrentCarIdx(0u)
    , mIsIn(false)
  { }

  template <class CAR,class ELEVATORTRANSPORT>
  void P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::initCars(CAR * stg, u32 count, u64 remoteaddr, bool isIn) {
    MFM_API_ASSERT_ARG(count == 0u || stg != 0);
    mCars = stg;
    mCarCount = count;
    mCurrentCarIdx = 0u;
    mRemoteBaseAddress = remoteaddr;
    mIsIn = isIn;
    memset_s(mCars,0u,count*sizeof(CAR));
  }

  template <class CAR,class ELEVATORTRANSPORT>
  CAR * P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::getCurrentCarIfAny() {
    if (mCurrentCarIdx >= mCarCount) return 0;
    return &mCars[mCurrentCarIdx];
  }

  template <class CAR,class ELEVATORTRANSPORT>
  bool P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::update(ELEVATORTRANSPORT & et) {
    { 
      static bool once;
      if (!once) {
        et.notice("1stupp2p\n");
        once = true;
      }
    }
    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      CAR& car = mCars[c];
      CarState cs = car.getCarState();

      if (cs == CarState::UNUSED) {
        // start with all cars on T6
        car.setCarState(mIsIn ? CarState::OUTBOUND_DEPARTED : CarState::OPEN,
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
          CarSig sig = car.getHeader();
          if (sig.mCarType == CarType::EMPTY) {
    { 
      static u8 once;
      if (once < 20u) {
        CarSig sig = car.getHeader();
        et.notice("%d-SCS(%x,%x,%x,%x)\n",once++,
                  sig.mCarMagic, sig.mCarNonce,
                  sig.mCarState, sig.mCarType);
        if (sig.mCarType == CarType::STANDARD) {
          CarSig fut = car.getStandardFooter();
          et.notice("FUT(%x,%x,%x,%x)=%d\n",
                    fut.mCarMagic, fut.mCarNonce,
                    fut.mCarState, fut.mCarType,car.isComplete());
        }
        once = true;
      }
    }            car.getContent().reset();         // clean out whole content
          }
          car.setCarState(CarState::OPEN,CarType::STANDARD); // now its standard
        } 
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
          {
            static bool once;
            if (!once) {
              et.notice("lbRTC(%d)\n",c);
              once = true;
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
