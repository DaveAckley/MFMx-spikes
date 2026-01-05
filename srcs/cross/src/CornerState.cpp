#include "CornerState.h"

namespace MFM {
  bool CornerState::isValid(Corner4 & minCorner) const  {
    Corner4 minc = C4_MIN;
    Wrap8 min = mState[(u8)minc];
    for (u32 i = C4_MIN; i <= C4_MAX; ++i) {
      if (mState[i].isLess(min)) {
        minc = (Corner4) i;
        min = mState[i];
      }
      for (u32 j = i+1; j <= C4_MAX; ++j) {
        if (mState[i] == mState[j] ||
            !mState[i].isComparable(mState[j]))
          return false;
      }
    }
    minCorner = minc;
    return true;
  }

  bool CornerState::dominates(const CornerState & other) const {
    if (mWord == other.mWord) return false;
    Corner4 unused;
    if (!isValid(unused) || !other.isValid(unused))
      return false;
        
    for (u32 i = C4_MIN; i <= C4_MAX; ++i) 
      if (other.mState[i] > mState[i])
        return false;

    return true;
  }

  bool CornerState::incrementPhaseGap() {
    Corner4 mincorner;
    if (!isValid(mincorner)) return false;
    CornerState cs = *this;
    for (u32 i = C4_MIN; i <= C4_MAX; ++i) {
      if (i == mincorner) continue;
      cs.mState[i].increment();
    }
    if (!cs.isValid(mincorner)) return false;
    *this = cs;
    return true;
  }

  bool CornerState::passToken() {
    Corner4 mincorner;
    if (!isValid(mincorner)) return false;
    Wrap8 t = mState[C4_MIN];
    for (u32 i = C4_MIN+1; i <= C4_MAX; ++i) // find max
      if (mState[i] > t) t = mState[i];
    mState[mincorner] = t.increment();
    return true;
  }

  u32 CornerState::getPhaseGap() const {
    Corner4 mincorner;
    if (!isValid(mincorner)) return 0u;
    Wrap8 min = mState[mincorner];

    Corner4 minc2 = clockwiseCorner4(mincorner);
    Wrap8 min2 = mState[minc2];
    for (u32 i = C4_MIN; i <= C4_MAX; ++i) { // find next to min 
      if (i == mincorner) continue;
      if (mState[i].isLess(min2)) {
        minc2 = (Corner4) i;
        min2 = mState[i];
      }
    }
    return min2.mData - min.mData;
  }
}
