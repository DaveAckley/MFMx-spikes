#ifndef RINGBUFFER_H  /* -*- C++ -*- */
#define RINGBUFFER_H
#include "itype.h"

namespace MFM {
  template <class T,u32 BITS> struct RingBuffer {
    static const u32 RING_BUFFER_BITS = BITS;
    static const u32 RING_BUFFER_SIZE = (1u<<RING_BUFFER_BITS);
    static const u32 RING_BUFFER_MASK = RING_BUFFER_SIZE-1u;
    T mRingBuffer[RING_BUFFER_SIZE];
    u32 mFirstUsedIdx;
    u32 mFirstFreeIdx;

    void reset() {
      mFirstFreeIdx = 0u;
      mFirstUsedIdx = 0u;
    }

    bool isEmpty() const { return mFirstUsedIdx == mFirstFreeIdx; }
    bool isFull() const {
      return
        ((mFirstFreeIdx + 1u) & RING_BUFFER_MASK) ==
        (mFirstUsedIdx & RING_BUFFER_MASK);
    }

    bool add(T item) {
      if (isFull()) return false;
      mRingBuffer[mFirstFreeIdx++ & RING_BUFFER_MASK] = item;
      return true;
    }

    bool remove(T& dest) {
      if (isEmpty()) return false;
      dest = mRingBuffer[mFirstUsedIdx++ & RING_BUFFER_MASK];
      return true;
    }
  };
}

#endif /*RINGBUFFER_H*/
