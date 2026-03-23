#pragma once   /* -*- C++ -*- */

#include "itype.h"

#include "TCCommon.h"
#include "AtomicLock.h"
#include "XUtils.h"

#ifndef BUILD_HOST
#include "HostBlock.h"
#include "FastLocal.h"
#endif

namespace MFM {

  template <class SUBEPSTG>
  struct alignas(16) EPStg {

    // self(): access this by subtype
    SUBEPSTG& self() { return static_cast<SUBEPSTG&>(*this); }
    SUBEPSTG const & self() const { return static_cast<SUBEPSTG const&>(*this); }

    // SERVICES
    u32 getCarSizeBytes() const { return self().getCarSizeBytes(); }
    u32 getCarCount() const { return self().getCarCount(); }

  };

  struct EP : public TCCommon {

    //// EP API: mandatory
    virtual u32 getCarSize() const = 0;
    virtual TCBase * getCarPtrIfAny(u8 carindex) const = 0;
    virtual TCOpsData & getOpsData(u8 carindex) = 0;
    virtual bool recvTC(TCBase & car, u8 carindex) = 0;
    virtual bool shipTC(TCBase & car, u8 carindex) = 0;
    //virtual bool initTC(TCBase & car, u8 carindex) = 0;

    //// EP API: optional
    virtual const char * getName() const { return "unnamed EP"; }
    virtual XPrinter & getLogToPrinter() const { return DEVNULL; }

    //// SERVICES
    TCBase * getCarPtr(u8 carindex) const {
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
      TCBase *ret = getCarPtrIfAny(carindex);

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

    void init(AtomicLock & lock, bool isIn, u32 carCount, bool carsIn = false) ;

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

    TCBase & getCar(u8 idx) const {
      MFM_API_ASSERT(isValidIndex(idx),ILLEGAL_ARGUMENT);
      TCBase * carp = getCarPtrIfAny(idx);
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

  protected:
    EP() = default;
    ~EP() = default;
    
  private:
    AtomicLock * mLockPtr;
    bool mIsIn;
    u8 mCarCount;
    //    u8 mOldestArrived;         // index else 255 if none/unknown
    //    u8 mOldestDeparted;        // index else 255 if none/unknown
    u8 mOldestHere, mHereCount;
    u8 mOldestGone, mGoneCount;
  };
}
