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
    memset(mCars,0u,count*sizeof(CAR));
  }

  template <class CAR,class ELEVATORTRANSPORT>
  CAR * P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::getCurrentCarIfAny() {
    if (mCurrentCarIdx >= mCarCount) return 0;
    return &mCars[mCurrentCarIdx];
  }

  template <class CAR,class ELEVATORTRANSPORT>
  bool P2PElevatorPlatform<CAR,ELEVATORTRANSPORT>::update(ELEVATORTRANSPORT & et) {
    bool ret = false;
    for (u32 c = 0u; c < mCarCount; ++c) {
      CAR& car = mCars[c];
      CarState cs = car.getCarState();
      switch (cs) {
      case CarState::UNUSED:
        // start with all cars on T6
        car.setCarState(mIsIn ? CarState::OUTBOUND_DEPARTED : CarState::OPEN );
        break;

      case CarState::INBOUND_DEPARTED:
      case CarState::OUTBOUND_DEPARTED:
        if (isArriving(cs)) 
          car.setCarState(CarState::OPEN); // You Have Arrived
        // if isDeparting, wait for external developments
        break;

      case CarState::OPEN:
        if (car.readyToClose()) {
          car.setCarState(CarState::CLOSED); // Please Buckle Up
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
