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

  struct EndPointAddress {
    BlockCode mBlockCode;
    u8 mBlockCodeIndex;
  };

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

    void initEP(EndPointAddress srcEPA, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn = false) ;
    void setDestEPA(EndPointAddress destEPA) {
      HBASSERT_EQ(getFastEPState(),EPState::INITTED);
      mDestEPA = destEPA;
      setFastEPState(EPState::CONFIGURED);
    }

    bool isInitted() const { return this->getFastEPState() >= EPState::INITTED; }

    bool isActive() const { return this->getFastEPState() >= EPState::ACTIVE; }

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

    EndPointAddress getSrcEPA() const { return mSrcEPA; }
    BlockCode getSrcBlockCode() const { return getSrcEPA().mBlockCode; }
    u8 getSrcBlockCodeIndex() const { return getSrcEPA().mBlockCodeIndex; }

    EndPointAddress getDestEPA() const { return mDestEPA; }
    BlockCode getDestBlockCode() const { return getDestEPA().mBlockCode; }
    u8 getDestBlockCodeIndex() const { return getDestEPA().mBlockCodeIndex; }

    u8 getHereCount() const { return mHereCount; }
    u8 getGoneCount() const { return mGoneCount; }

    void activate() {
      HBPTAG(ATCIV/src,getNameFromBlockCode(mSrcEPA.mBlockCode));
      HBPTAG(s,mSrcEPA.mBlockCodeIndex);
      //HBPTAG(dest,getNameFromBlockCode(mDestEPA.mBlockCode));
      HBPTAG(d,mDestEPA.mBlockCodeIndex);
      HBASSERT_EQ(getFastEPState(),EPState::CONFIGURED);
      setFastEPState(EPState::ACTIVE);
    }
    
  protected:
    EP() = default;
    ~EP() = default;
    
  private:
    EndPointAddress mSrcEPA; //< Local end
    EndPointAddress mDestEPA;   //< Remote end
    EPState getFastEPState() const { return mFastEPState; }
    void setFastEPState(EPState newstate) {
      HBASSERT_NE(getFastEPState(),newstate);
      mFastEPState = newstate;
      if (mFastEPState >= EPState::CONFIGURED) 
        this->setPublicEPState(mFastEPState);
    }

    EPState mFastEPState;       //< shadowed in L1Data
    AtomicLock * mLockPtr;
    u8 mCarCount;
    bool mIsIn;
    bool mCarsStartIn;
    u8 mOldestHere, mHereCount;
    u8 mOldestGone, mGoneCount;
  };
}

#include "EP.tcc"

