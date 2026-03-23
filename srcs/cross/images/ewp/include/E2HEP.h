#pragma once /* -*- C++ -*- */

#include "CrossEP.h"
#include "Printf.h"

namespace MFM {
  class T6ElevatorTransport; // FORWARD
  
  class E2HEP : public CrossEP<E2HEP, EWBlock> {
  public:
    typedef CARTYPE EWCar;

    //// EP API
    const char * getNameEPI() const { return "E2HEP"; }
    u32 getCarCountEPI() const { return EWCarStorage::CAR_COUNT; }
    bool isInEPI() const { return false; } // we are not the 'in' end
    CARTYPE * getCarStgEPI() const { return mCars; }

    s32 updateEPI() {
      return update() ? 0 : -1;
    }

    E2HEP() ;
    bool update() ;

    void initCars(u32 ourewpindex,
                  U8C ournoc0,
                  EWCar * stg, CarOpsTimers * tms,
                  U8C hubnoc0, u32 hubewhubblockaddr) ;

    u64 getCarRemoteAddress() const { return computeCarDestination(mCurrentCarIdx); }

    //    Printer & to_repr(Printer& to) const ;
    // Printer & print(Printer& to, BaseCar<EWBlock> & bc) const ;

  private:

    u32 computeCarDestination(u32 carnum) const {
      return mEWCarStorage + carnum * sizeof(EWCar);
    }
    u32 mOurEWPIndex;
    u32 mEWHUBAddress;          // remote L1 addr of EWCarStorage[8]
    u32 mEWCarStorage;          // remote L1 addr of EWCarStorage[mOurEWPIndex]
    U8C mOurNoC0;
    U8C mHubNoC0;
    CARTYPE *mCars;              // [0..mCarCount - 1]
    CarOpsTimers * mCarOpsTimers; // ditto
    u32 mCurrentCarIdx;
  };
}

