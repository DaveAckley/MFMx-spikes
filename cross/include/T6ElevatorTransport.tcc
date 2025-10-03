#include <cstring>

namespace MFM {    /* -*- C++ -*- */

  template <typename TYPE>
  void TCarManager<TYPE>::init(TYPE& base, u32 count) {
    mTCarBasePtr = &base;
    mTCarCount = count;
    mCurrentIndex = 0u;
    memset(mTCarBasePtr, 0, count*sizeof(TYPE));
  }
}
