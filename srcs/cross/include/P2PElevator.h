#ifndef P2PELEVATOR_H    /* -*- C++ -*- */
#define P2PELEVATOR_H

#include "itype.h"
#include "BaseCar.h"
#include "FATAL.h"
#include <stdarg.h>

namespace MFM {

  struct ElevatorTransportBase {
    void notice(const char * fmt, ...) ;
  };

  class P2PBasePlatform {
  };

  // Reqmts:
  //  CAR isa BaseCar
  template <class CAR,class ELEVATORTRANSPORT>
  class P2PElevatorPlatform : public P2PBasePlatform {
  public:
    P2PElevatorPlatform() ;
    void initCars(CAR * stg, u32 count, u64 remoteaddr, bool isIn) ;
    bool update(ELEVATORTRANSPORT & et) ;
    u32 getCurCarIdx() const { return mCurrentCarIdx; }
    u64 getCarRemoteAddress() const { return computeCarDestination(mCurrentCarIdx); }

    CAR * getCurrentCarIfAny() ;
    CarState departingState() const {
      return mIsIn ? CarState::OUTBOUND_DEPARTED : CarState::INBOUND_DEPARTED;
    }
    CarState arrivingState() const {
      return mIsIn ? CarState::INBOUND_DEPARTED : CarState::OUTBOUND_DEPARTED;
    }
    bool isArriving(CarState cs) const { return cs == arrivingState(); }
    bool isDeparting(CarState cs) const { return cs == departingState(); }
    
  private:
    u64 computeCarDestination(u32 carnum) const {
      return mRemoteBaseAddress + carnum*sizeof(CAR);
    }
    u32 nextCarIndex() {
      u32 ret = mCurrentCarIdx+1u;
      if (ret >= mCarCount) ret = 0u;
      return ret;
    }
    u64 mRemoteBaseAddress;     // of far mCars
    CAR *mCars;
    u32 mCarCount;
    u32 mCurrentCarIdx;
    bool mIsIn;                 // true if host, false if t6
  };

  template <class CAR>
  class P2PElevator {
  public:
    void sendCar(bool source, CAR &) = 0;
  private:
    u64 mRemoteBaseAddress;
  };
}

#include "P2PElevator.tcc"

#endif /* P2PELEVATOR_H */
