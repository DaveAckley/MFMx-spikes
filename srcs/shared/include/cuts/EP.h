#pragma once   /* -*- C++ -*- */

#include "itype.h"

#include "TCCommon.h"
#include "AtomicLock.h"
#include "XUtils.h"

namespace MFM {

  struct EP : public TCCommon {
    using CAR_TYPE = TYPE;
    static constexpr u32 CAR_COUNT = COUNT;
    struct CarStorageBlock {
      CAR_TYPE mCarStg[CAR_COUNT];
    };

    // self(): access this by subtype
    SUB& self() { return static_cast<SUB&>(*this); }
    SUB const & self() const { return static_cast<SUB const&>(*this); }

    // EP API
    const char * getName() const { return self().getNameEP(); }
    bool isIn() const { return self().isInEP(); }
    CarStorageBlock & getCarStg() const { return self().getCarStgEP(); }
    bool ship(CAR_TYPE & car, u32 carnum) { return self().shipEP(car,carnum); }
    AtomicLock & getPlatformLock() { return self().getPlatformLockEP(); }

    // EP SERVICES
    void reset() {
      memset_s(this,'\0',sizeof(*this));
      mOldestArrived = U8_MAX;
      mOldestDeparted = U8_MAX;
    }

    XPrinter & logTo() {
      if (mLogToPrinter) return *mLogToPrinter;
      FAIL(ILLEGAL_STATE);
    }

    TCOpsData * getTCOpsDataIfAny(u8 index) {
      if (!isValidIndex(index)) return 0;
      return &mTCOpsData[index];
    }

    TCOpsData & getTCOpsData(u8 index) {
      MFM_API_ASSERT(isValidIndex(index),ILLEGAL_ARGUMENT);
      return mTCOpsData[index];
    }

    CAR_TYPE & getCar(u8 idx) const {
      MFM_API_ASSERT(isValidIndex(idx),ILLEGAL_ARGUMENT);
      return getCarStg().mCarStg[i];
    }
    CAR_TYPE * getCarIfAny(u8 idx) const {
      if (!isValidIndex(idx)) return 0;
      return &getCar(idx);
    }

    static bool isValidIndex(u8 idx) { return idx < CAR_COUNT; }
    static u8 incrementIndex(u8 idx) {
      if (!isValidIndex(idx)) return U8_MAX; // unknown stays unknown
      if (idx == CAR_COUNT-1u) return 0u;
      return idx+1u;
    }

    TCState departingState() const {
      return isIn() ? TCState::OUTBOUND_DEPARTED : TCState::INBOUND_DEPARTED;
    }
    TCState arrivingState() const {
      return isIn() ? TCState::INBOUND_DEPARTED : TCState::OUTBOUND_DEPARTED;
    }
    bool isArriving(TCState cs) const { return cs == arrivingState(); }
    bool isDeparting(TCState cs) const { return cs == departingState(); }

    bool updateOps() ;

  protected:
    EP() = default;

  private:
    AtomicLock mPlatformLock;
    TCOpsData mTCOpsData[CAR_COUNT];
    XPrinter * mLogToPrinter;
    u8 mOldestArrived;         // index else 255 if none/unknown
    u8 mOldestDeparted;        // index else 255 if none/unknown
  };
}

