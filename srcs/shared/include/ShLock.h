#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "Fail.h"

namespace MFM {
  struct ShLock {
    // ShLock API
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool tryLock() = 0;
    virtual bool peekLock() const = 0; //< advisory, unreliable; only for debugging
  };
  
  struct ScopeShLock {
    ScopeShLock(ShLock & shl)
      : mOurShLock(shl)
    {
      mOurShLock.lock();
    }

    // let's just define that there's no heap for these guys, okay?
    void * operator new(size_t) { FAIL(ILLEGAL_STATE); }
    void operator delete(void *, size_t) { FAIL(ILLEGAL_STATE); }

    virtual ~ScopeShLock() {
      mOurShLock.unlock();
    }
    ShLock & mOurShLock;
  };
}

