#pragma once /* -*- C++ -*- */
#include <mutex>
#include <unistd.h>

#include "itype.h"

namespace MFM {
  struct OurMutex {
    OurMutex(const char * name)
      : mStdMutex()
      , mMutexName(name)
      , mThreadLockerId(0u)
    { }

    std::mutex mStdMutex;
    const char * mMutexName;
    u32 mThreadLockerId;

    void print(FILE * file) ;

    void lock() {
      mStdMutex.lock();
      mThreadLockerId = gettid();
    }

    void unlock() {
      mThreadLockerId = 0u;
      mStdMutex.unlock();
    }
  };
  
  struct OurScopeLock {
    OurScopeLock(OurMutex & mut)
      : mOurMutex(mut)
    {
      mOurMutex.lock();
    }
    ~OurScopeLock() {
      mOurMutex.unlock();
    }
    OurMutex & mOurMutex;
  };
}

