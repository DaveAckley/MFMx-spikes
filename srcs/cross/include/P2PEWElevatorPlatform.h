#pragma once /* -*- C++ -*- */

#include "EP.h"
#include "BaseCar.h"
#include "AtomicLock.h"
#include "TC.h"

namespace MFM {
  class T6ElevatorTransport; // FORWARD
  
  class P2PEWElevatorPlatform : public EP<P2PEWElevatorPlatform,EWBlock> {
  public:
    typedef CARTYPE EWCar;
    static constexpr u32 CAR_COUNT = EWCarStorage::CAR_COUNT;
    static constexpr u32 CAR_STORAGE_SIZE = CAR_COUNT*sizeof(EWCar);
    static constexpr u32 CAR_OPS_TIMERS_SIZE = CAR_COUNT*sizeof(CarOpsTimers);

    // EP IMPL
    const char * getNameEPI() const { return "P2PEW"; }
    u32 getCarCountEPI() const { return CAR_COUNT; }
    bool isInEPI() const { return true; }
    CARTYPE * getCarStgEPI() const ;
    CarOpsTimers * getCarOpsTimersEPI() const ;
    bool shipEPI(CARTYPE & car, u32 carnum) ;

    P2PEWElevatorPlatform(EWCar * stg, CarOpsTimers * tms) ;

    //    bool sendCar() ;            // ASSUMES PLATFORM LOCK IS HELD

  private:

    u64 computeCarDestination(u32 carnum) const {
      return mRemoteBaseAddress + carnum*sizeof(EWCar);
    }
    u64 mRemoteBaseAddress;     // of far mCars
    EWCar *mCars;              // [0..mCarCount - 1]
    CarOpsTimers * mCarOpsTimers; // ditto
  };
}

