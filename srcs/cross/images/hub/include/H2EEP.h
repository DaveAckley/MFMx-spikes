#pragma once /* -*- C++ -*- */

#include "CrossUtils.h"

#include "BaseCar.h"
#include "AtomicLock.h"
#include "TransportBlock.h"
#include "Printf.h"

namespace MFM {
  class T6ElevatorTransport; // FORWARD
  
  class H2EEP {
  public:
    typedef BaseCar<EWBlock> EWCar;
    H2EEP() { }
    void init() ;
    void initCars(U8C ournoc0, u8 ewpidx, U8C ewpnoc0, EWCar * stg, BaseCarMetadata * meta, u32 count, u32 remoteL1Addr, bool isIn) ;
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

    BaseCarMetadata * getBaseCarMetadata() const { return mCarMetadata; }

  private:
    AtomicLock mPlatformLock;

    u32 computeCarDestination(u32 carnum) const {
      return mRemoteBaseAddress + carnum*sizeof(EWCar);
    }
    static constexpr u32 MAX_CELL_INDICES = 14u;
    //    U8C mNoC0sByEWPTypeIndex[MAX_CELL_INDICES];
    U8C mOurNoC0;
    u8 mEWPIdx;
    U8C mEWPNoC0;
    u32 mRemoteBaseAddress;     // of far mCars
    EWCar *mCars;              // [0..mCarCount - 1]
    BaseCarMetadata * mCarMetadata; // ditto
    u32 mCarCount;
    u32 mCurrentCarIdx;
    bool mIsIn;                 // true if host, false if t6
  };
}
