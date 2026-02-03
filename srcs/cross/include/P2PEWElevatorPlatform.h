#pragma once /* -*- C++ -*- */

#include "BaseCar.h"
#include "AtomicLock.h"
#include "TransportBlock.h"

namespace MFM {
  class T6ElevatorTransport; // FORWARD
  
  class P2PEWElevatorPlatform {
  public:
    typedef BaseCar<EWBlock> EWCar;
    P2PEWElevatorPlatform() ;
    void initCars(EWCar * stg, BaseCarMetadata * meta, u32 count, u64 remoteaddr, bool isIn) ;
    bool update(T6ElevatorTransport & et) ; // TAKES PLATFORM LOCK
    bool sendCar() ;            // ASSUMES PLATFORM LOCK IS HELD

    u32 getCurrentCarIndex() const { return mCurrentCarIdx; }
    u32 nextCarIndex() {
      u32 ret = mCurrentCarIdx+1u;
      if (ret >= mCarCount) ret = 0u;
      return ret;
    }
    void advanceToNextCar() { mCurrentCarIdx = nextCarIndex(); }
    u64 getCarRemoteAddress() const { return computeCarDestination(mCurrentCarIdx); }

    AtomicLock & getPlatformLock() { return mPlatformLock; }
    EWCar * getCurrentCarIfAny() ;
    CarState departingState() const {
      return mIsIn ? CarState::OUTBOUND_DEPARTED : CarState::INBOUND_DEPARTED;
    }
    CarState arrivingState() const {
      return mIsIn ? CarState::INBOUND_DEPARTED : CarState::OUTBOUND_DEPARTED;
    }
    bool isArriving(CarState cs) const { return cs == arrivingState(); }
    bool isDeparting(CarState cs) const { return cs == departingState(); }
    
  private:
    AtomicLock mPlatformLock;

    u64 computeCarDestination(u32 carnum) const {
      return mRemoteBaseAddress + carnum*sizeof(EWCar);
    }
    u64 mRemoteBaseAddress;     // of far mCars
    EWCar *mCars;              // [0..mCarCount - 1]
    BaseCarMetadata * mCarMetadata; // ditto
    u32 mCarCount;
    u32 mCurrentCarIdx;
    bool mIsIn;                 // true if host, false if t6
  };
}

