#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "Wrap8.h"

namespace MFM {
  union CornerState {
    u32 mWord;
    Wrap8 mState[4]; // [Corner4]

    void init() { mWord = 0u; }

    /** false if *this or other is invalid, else return true with
        result < 0:  this min < other min
        result == 0: this min == other min (but it's possible mWord != other.mWord)
        result > 0:  this min > other min
     */
    bool compare(const CornerState & other, s32 & result) const {
      FAIL(INCOMPLETE_CODE);
    }
    
    /** return true if *this and other are both valid, are unequal to
        each other, and all components of *this are >= the
        corresponding component of other. Otherwise returns false */
    bool dominates(const CornerState & other) const ;

    /** return 0 if *this is invalid, otherwise return the difference
        between the smallest and the next-smallest components of
        *this */
    u32 getPhaseGap() const ;

    /** return false if *this is invalid, or if incrementing the phase
        gap would render *this invalid. Otherwise, increments all
        components except for the mincorner, and returns true */
    bool incrementPhaseGap() ;

    /** return false if *this is invalid. Otherwise, increments
        mincorner to be one greater than maxcorner, and returns
        true */
    bool passToken() ;

    bool isValid(Corner4 & minCorner) const ;
    bool isValid() const { Corner4 unused; return isValid(unused); }
  };
}
