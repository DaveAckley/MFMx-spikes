#pragma once /* -*- C++ -*- */

#include "BaseCar.h"
#include "AtomicLock.h"
#include "TransportBlock.h"
#include "Printf.h"

namespace MFM {
  class T6ElevatorTransport; // FORWARD
  
  class E2HEP {
  public:
    typedef BaseCar<EWBlock> EWCar;
    E2HEP() ;
    void initCars(u32 ourewpindex,
                  U8C ournoc0,
                  EWCar * stg, BaseCarMetadata * meta, u32 count,
                  U8C hubnoc0, u32 hubewhubblockaddr,
                  bool isIn) ;
    bool update() ; // TAKES PLATFORM LOCK
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

    Printer & to_repr(Printer& to) const ;
    Printer & print(Printer& to, BaseCar<EWBlock> & bc) const ;

  private:
    AtomicLock mPlatformLock;

    u32 computeCarDestination(u32 carnum) const {
      return mEWCarStorage + carnum * sizeof(EWCar);
    }
    u32 mOurEWPIndex;
    u32 mEWHUBAddress;          // remote L1 addr of EWCarStorage[8]
    u32 mEWCarStorage;          // remote L1 addr of EWCarStorage[mOurEWPIndex]
    U8C mOurNoC0;
    U8C mHubNoC0;
    EWCar *mCars;              // [0..mCarCount - 1]
    BaseCarMetadata * mCarMetadata; // ditto
    u32 mCarCount;
    u32 mCurrentCarIdx;
    bool mIsIn;                 // true if host, false if t6
  };
}

