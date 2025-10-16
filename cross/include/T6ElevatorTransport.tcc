/* -*- C++ -*- */

#include <cstring>

namespace MFM {

  template <typename TYPE>
  void TCarManager<TYPE>::init(TYPE& base, u32 count) {
    mTCarBasePtr = &base;
    mCount = count;
    mCurrentIndex = 0u;
    memset(mTCarBasePtr, 0, mCount*sizeof(TYPE));
  }

  template <typename TYPE>
  void TCarManager<TYPE>::update() {
    if (!mCarLock.tryLock()) return;
    // BEGIN CRITICAL SECTION
    TYPE & curCar = mTCarBasePtr[mCurrentIndex];
    u32 avail = curCar.spaceRemaining();

    u32 next = nextCarIndex();
    TYPE & nextCar = mTCarBasePtr[next];
    if (avail < 50u) {
      //      XXX
    }
    // END CRITICAL SECTION
    mCarLock.releaseLock();
  }

}
