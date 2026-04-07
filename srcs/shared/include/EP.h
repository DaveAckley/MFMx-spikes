#pragma once   /* -*- C++ -*- */

#include "itype.h"

#include "EPState.h"
#include "TCCommon.h"
#include "AtomicLock.h"
#include "XUtils.h"
#include "BlockCode.h"
#include "Debug.h"

#ifndef BUILD_HOST
#include "HostBlock.h"
#include "FastLocal.h"
#endif

namespace MFM {

  template<class SUBEP, class SUBTC>
  struct EP {

    // self(): access this by subtype
    SUBEP& self() { return static_cast<SUBEP&>(*this); }
    SUBEP const & self() const { return static_cast<SUBEP const&>(*this); }

    // "API": SUBEP must implement all of these!
    void setPublicEPState(EPState newstate) { self().setPublicEPState(newstate); }
    u32 getCarSize() const { return self().getCarSize(); }
    SUBTC * getClosedTCPtrIfAny() const { return self().getClosedTCPtrIfAny(); }
    SUBTC * getCarPtrIfAny(u8 carindex) const { return self().getCarPtrIfAny(carindex); }
    TCOpsData & getOpsData(u8 carindex) { return self().getOpsData(carindex); }
    bool recvTC(SUBTC & car, u8 carindex) { return self().recvTC(car, carindex); }
    bool shipTC(SUBTC & car, u8 carindex) { return self().shipTC(car, carindex); }
    //unclear we want to go this way: bool isRemoteHost() const { return self().isRemoteHost(); }

    const char * getName() const { return self().getName(); }
    //virtual XPrinter & getLogToPrinter() const { return DEVNULL; }

    //// SERVICES
    SUBTC * getCarPtr(u8 carindex) const {
      SUBTC *ret = getCarPtrIfAny(carindex);
      MFM_API_ASSERT_NONNULL(ret);
      return ret;
    }

    void initEP(BlockCode bc, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn = false) ;

#if 0
    bool launchAllCars() {
      FAIL(DEPRECATED);
      AtomicScopeLock guard(getPlatformLock());
      if (mCarsStartIn == mIsIn) { // all cars here
        MFM_API_ASSERT(mHereCount == 0u && mOldestHere == U8_MAX,ILLEGAL_STATE);
        mHereCount = mCarCount;
        mOldestHere = 0u;
        mGoneCount = 0u;
        mOldestGone = U8_MAX;
      } else {                  // all cars gone
        mHereCount = 0;
        mOldestHere = U8_MAX;
        mGoneCount = mCarCount;
        mOldestGone = 0u;
      }
      return true;
    }
#endif

    bool isInitted() const { return mLockPtr != 0; } // sufficient proxy?

    u8 getCarCount() const { return mCarCount; }
    bool isIn() const { return mIsIn; }

    AtomicLock * getLockPtrDebug() const { return mLockPtr; }

    AtomicLock & getPlatformLock() {
      MFM_API_ASSERT_NONNULL(mLockPtr);
      return *mLockPtr;
    }

    void reset() { memset_s(this,'\0',sizeof(*this)); }

    bool isValidIndex(u8 idx) const { return idx < mCarCount; }
    u8 incrementIndex(u8 idx) const {
      if (!isValidIndex(idx)) return U8_MAX; // unknown stays unknown
      if (idx == mCarCount-1u) return 0u;
      return idx+1u;
    }

    SUBTC & getCar(u8 idx) const {
      MFM_API_ASSERT(isValidIndex(idx),ILLEGAL_ARGUMENT);
      SUBTC * carp = getCarPtrIfAny(idx);
      MFM_API_ASSERT_NONNULL(carp);
      return *carp;
    }

    TCState departingState() const {
      return mIsIn ? TCState::OUTBOUND_DEPARTED : TCState::INBOUND_DEPARTED;
    }
    TCState arrivingState() const {
      return mIsIn ? TCState::INBOUND_DEPARTED : TCState::OUTBOUND_DEPARTED;
    }
    bool isArriving(TCState cs) const { return cs == arrivingState(); }
    bool isDeparting(TCState cs) const { return cs == departingState(); }

    bool updateOps() ;
    BlockCode getDestBlockCode() const { return mDestBlockCode; }
    u8 getDestBlockCodeIndex() const { return mDestBlockCodeIndex; }
    void setDestBlockCodeIndex(u8 blkidx) {
      HBASSERT_EQ(getFastEPState(),EPState::INITTED);
      mDestBlockCodeIndex = blkidx;
      setFastEPState(EPState::CONFIGURED);
    }

    u8 getHereCount() const { return mHereCount; }
    u8 getGoneCount() const { return mGoneCount; }

    void activate() {
      HBNOTE("ATCIV");
      HBPVAL((void*)this);
      HBASSERT_EQ(getFastEPState(),EPState::CONFIGURED);
      setFastEPState(EPState::ACTIVE);
    }
    
  protected:
    EP() = default;
    ~EP() = default;
    
  private:
    EPState getFastEPState() const { return mFastEPState; }
    void setFastEPState(EPState newstate) {
      HBASSERT_NE(getFastEPState(),newstate);
      mFastEPState = newstate;
      if (mFastEPState >= EPState::CONFIGURED)
        this->setPublicEPState(mFastEPState);
    }

    EPState mFastEPState;       //< shadowed in L1Data
    AtomicLock * mLockPtr;
    BlockCode mDestBlockCode;
    u8 mDestBlockCodeIndex;
    u8 mCarCount;
    bool mIsIn;
    bool mCarsStartIn;
    u8 mOldestHere, mHereCount;
    u8 mOldestGone, mGoneCount;
  };
}

#include "EP.tcc"

