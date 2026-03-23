#pragma once   /* -*- C++ -*- */

#include "itype.h"

#include "BaseCar.h"
#include "AtomicLock.h"
#include "XUtils.h"

namespace MFM {
  // (CRTP) Base class for Elevator Platform functions
  template <class SUB, class CAR_CONTENT>
  struct EP {
    typedef BaseCar<CAR_CONTENT> CARTYPE;

    // self(): access this by subtype
    SUB& self() { return static_cast<SUB&>(*this); }
    SUB const & self() const { return static_cast<SUB const&>(*this); }

    // EP API
    const char * getName() const { return self().getNameEPI(); }
    u32 getCarCount() const { return self().getCarCountEPI(); }
    bool isIn() const { return self().isInEPI(); }
    CARTYPE * getCarStg() const { return self().getCarStgEPI(); }
    CarOpsTimers * getCarOpsTimers() const { return self().getCarOpsTimersEPI(); }
    bool ship(CARTYPE & car, u32 carnum) { return self().shipEPI(car,carnum); }

    // EP SERVICES

    AtomicLock & getPlatformLock() { return mPlatformLock; }
    CARTYPE * getCurrentCarIfAny() { 
      CARTYPE * ret = getCarStg();
      if (!ret || mCurrentCarIdx >= getCarCount()) return 0;
      return &ret[mCurrentCarIdx];
    }
    CarOpsTimers & getCurrentCarOps() {
      CarOpsTimers * tms = getCarOpsTimers();
      MFM_API_ASSERT_NONNULL(tms);
      return tms[mCurrentCarIdx];
    }
    u32 getCurrentCarIndex() const { return mCurrentCarIdx; }
    u32 nextCarIndex() {
      u32 ret = mCurrentCarIdx+1u;
      if (ret >= getCarCount()) ret = 0u;
      return ret;
    }
    void advanceToNextCar() { mCurrentCarIdx = nextCarIndex(); }

    CarState departingState() const {
      return isIn() ? CarState::OUTBOUND_DEPARTED : CarState::INBOUND_DEPARTED;
    }
    CarState arrivingState() const {
      return isIn() ? CarState::INBOUND_DEPARTED : CarState::OUTBOUND_DEPARTED;
    }
    bool isArriving(CarState cs) const { return cs == arrivingState(); }
    bool isDeparting(CarState cs) const { return cs == departingState(); }

    bool updateOps() ;

  protected:
    EP() = default;

  private:
    u32 mCurrentCarIdx;
    AtomicLock mPlatformLock;
  };
}

#include "EP.tcc"
