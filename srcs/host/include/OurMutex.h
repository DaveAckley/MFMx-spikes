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

    bool tryLock() {
      if (mStdMutex.try_lock()) {
        mThreadLockerId = gettid();
        return true;
      }
      return false;
    }

    void lock() {
      mStdMutex.lock();
      mThreadLockerId = gettid();
    }

    void unlock() {
      mThreadLockerId = 0u;
      mStdMutex.unlock();
    }

    bool smellyPeekLock() const {
      return mThreadLockerId != 0u;
    }
  };
}

