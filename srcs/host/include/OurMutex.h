#pragma once /* -*- C++ -*- */
#include <mutex>
#include <unistd.h>

#include "itype.h"

namespace MFM {
  struct OurMutex {
    OurMutex(const char * name)
      : mStdMutex()
      , mMutexName(name)
      , mThreadLocker(0u)
    { }

    std::mutex mStdMutex;
    const char * mMutexName;
    u32 mThreadLocker;

    void print(FILE * file) ;

    void lock() {
      mStdMutex.lock();
      mThreadLocker = gettid();
    }

    void unlock() {
      mThreadLocker = 0u;
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

