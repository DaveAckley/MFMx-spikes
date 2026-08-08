#ifndef RINGBUFFER_H  /* -*- C++ -*- */
#define RINGBUFFER_H
#include "itype.h"
#include "XUtils.h" // for memset_s

#ifndef BUILD_HOST
#include "CrossUtils.h" // for writeCommit*
#endif

namespace MFM {
  template <class T,u32 BITS> struct RingBuffer {
    static const u32 RING_BUFFER_BITS = BITS;
    static const u32 RING_BUFFER_SIZE = (1u<<RING_BUFFER_BITS);
    static const u32 RING_BUFFER_MASK = RING_BUFFER_SIZE-1u;
    T mRingBuffer[RING_BUFFER_SIZE];
    u32 mFirstUsedIdx;
    u32 mFirstFreeIdx;

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    void reset() {
      mFirstFreeIdx = 0u;
      mFirstUsedIdx = 0u;
    }

    u32 lengthish() const {
      return mFirstFreeIdx - mFirstUsedIdx;
    }

    bool hasRoomForNMore(u32 n) const {
      if (n >= RING_BUFFER_SIZE/2) return false; // SHOULD FAIL INSTEAD
      if (unlikely(mFirstFreeIdx >= U32_MAX-n)) {
        u32 lim = mFirstUsedIdx & RING_BUFFER_MASK;
        for (u32 i = 1u; i <= n; ++i)
          if (((mFirstFreeIdx + i) & RING_BUFFER_MASK) == lim)
            return false;
        return true;
      }
      return mFirstFreeIdx - mFirstUsedIdx < RING_BUFFER_SIZE - n ;
    }

    bool isEmpty() const { return mFirstUsedIdx == mFirstFreeIdx; }
    bool isFull() const {
      return
        ((mFirstFreeIdx + 1u) & RING_BUFFER_MASK) ==
        (mFirstUsedIdx & RING_BUFFER_MASK);
    }

    bool add(T item) {
      if (isFull()) return false;
#ifndef BUILD_HOST
      // ensure new item store is complete
      writeCommitL1(&mRingBuffer[mFirstFreeIdx & RING_BUFFER_MASK], item);
#else      
      // or at least have a sequence point, host-side.
      mRingBuffer[mFirstFreeIdx & RING_BUFFER_MASK] = item;
#endif
      ++mFirstFreeIdx;          // before incrementing the pointer
      return true;
    }

    bool remove(T& dest) {
      if (isEmpty()) return false;
#ifndef BUILD_HOST
      // ensure dest store is complete
      writeCommitL1(&dest, mRingBuffer[mFirstUsedIdx & RING_BUFFER_MASK]);
#else
      // or at least have a sequence point, host-side.
      dest = mRingBuffer[mFirstUsedIdx & RING_BUFFER_MASK];
#endif
      ++mFirstUsedIdx;          // before incrementing the pointer
      return true;
    }
  };
}

#endif /*RINGBUFFER_H*/
