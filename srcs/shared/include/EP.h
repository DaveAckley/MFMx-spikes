#pragma once   /* -*- C++ -*- */

#include "itype.h"

#include "TCCommon.h"
#include "AtomicLock.h"
#include "XUtils.h"
#include "BlockCode.h"

#ifndef BUILD_HOST
#include "HostBlock.h"
#include "FastLocal.h"
#endif

namespace MFM {

  template<class SUBEP, class SUBTC>
  struct EP : public TCCommon {
    //    using TCCommon::TCMarker;
    //    using TCCommon::TCWord;

    // self(): access this by subtype
    SUBEP& self() { return static_cast<SUBEP&>(*this); }
    SUBEP const & self() const { return static_cast<SUBEP const&>(*this); }

    // "API": SUBEP must implement all of these!
    u32 getCarSize() const { return self().getCarSize(); }
    SUBTC * getCarPtrIfAny(u8 carindex) const { return self().getCarPtrIfAny(carindex); }
    TCOpsData & getOpsData(u8 carindex) { return self().getOpsData(carindex); }
    bool recvTC(SUBTC & car, u8 carindex) { return self().recvTC(car, carindex); }
    //    bool turnTC(SUBTC & car, u8 carindex) { return self().turnTC(car, carindex); }
    bool shipTC(SUBTC & car, u8 carindex) { return self().shipTC(car, carindex); }
    //bool initTC(TCBase & car, u8 carindex) = 0;

    const char * getName() const { return self().getName(); }
    //virtual XPrinter & getLogToPrinter() const { return DEVNULL; }

    //// SERVICES
    SUBTC * getCarPtr(u8 carindex) const {
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('*',hartChar(fAll.mHartNum));
        theHostBlock.addBytes('0'+carindex,hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
    SUBTC *ret = getCarPtrIfAny(carindex);

#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes('?',hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
      MFM_API_ASSERT_NONNULL(ret);
      return ret;
    }

    void init(BlockCode bc, u8 blkIdx, AtomicLock & lock, bool isIn, u32 carCount, bool carsIn = false) ;

    u8 getCarCount() const { return mCarCount; }
    bool isIn() const { return mIsIn; }

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

    u8 getHereCount() const { return mHereCount; }
    u8 getGoneCount() const { return mGoneCount; }
  protected:
    EP() = default;
    ~EP() = default;
    
    
  private:
    AtomicLock * mLockPtr;
    BlockCode mDestBlockCode;
    u8 mDestBlockCodeIndex;
    u8 mCarCount;
    bool mIsIn;
    u8 mOldestHere, mHereCount;
    u8 mOldestGone, mGoneCount;
  };
}

#include "EP.tcc"

