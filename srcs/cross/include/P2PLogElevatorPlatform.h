#ifndef P2PLOGELEVATOR_H    /* -*- C++ -*- */
#define P2PLOGELEVATOR_H

#include <stdarg.h>

#include "itype.h"
#include "BaseCar.h"
#include "FATAL.h"
#include "AtomicLock.h"
#include "CrossUtils.h"
#include "TC.h"
#include "CrossEP.h"

namespace MFM {

  class T6ElevatorTransport; // FORWARD

  typedef BaseCar<LogBlock> LogCar;

  class P2PLogElevatorPlatform : public CrossEP<P2PLogElevatorPlatform,LogCar> {
  public:
    P2PLogElevatorPlatform() ;
    void initCars(LogCar * stg, CarOpsTimers * tms, u32 count, u64 remoteaddr, bool isIn) ;
    bool update(T6ElevatorTransport & et) ; // TAKES PLATFORM LOCK
    bool sendByte(u8 byte) ;                // ASSUMES PLATFORM LOCK IS HELD
    //    void sendLastByteOrDie(u8 byte) ;       // ASSUMES PLATFORM LOCK IS HELD
    u32 getCurrentCarIndex() const { return mCurrentCarIdx; }
    u32 nextCarIndex() {
      u32 ret = mCurrentCarIdx+1u;
      if (ret >= mCarCount) ret = 0u;
      return ret;
    }
    void advanceToNextCar() { mCurrentCarIdx = nextCarIndex(); }
    u64 getCarRemoteAddress() const { return computeCarDestination(mCurrentCarIdx); }

    AtomicLock & getPlatformLock() { return mPlatformLock; }
    LogCar * getCurrentCarIfAny() ;
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
      return mRemoteBaseAddress + carnum*sizeof(LogCar);
    }
    u64 mRemoteBaseAddress;     // of far mCars
    LogCar *mCars;              // [0..mCarCount - 1]
    CarOpsTimers * mCarOpsTimers; // ditto
    u32 mCarCount;
    u32 mCurrentCarIdx;
    bool mIsIn;                 // true if host, false if t6
  };
}

#endif /* P2PLOGELEVATOR_H */
