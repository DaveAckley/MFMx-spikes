#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "Fail.h"
#include "ShLock.h"

namespace MFM {

  inline bool tryLockASM(void* addr) {
    bool ret;
    asm volatile (
  "lw t1, (%1)      # get lock value\n\t"
  "bnez t1,1f       # jump ahead unless free\n\t"
  "li t1, 1         # init swap value\n\t"
  "amoswap.w t0, t1, (%1)   # try for lock\n"
"1:\txori %0,t1,1   # result is !bottom bit of t1\n\t"
        : "=r"(ret)             // output to ret reg
        : "r"(addr)             // input arg reg
        : "t0", "t1"            // clobbered temps
        );
    return ret;
  }
  
  inline void acquireLockASM(void* addr) {
    asm volatile (
  "li t0, 1         # init swap value\n"
"1:\tlw t1, (%0)    # get lock value\n\t"
  "bnez t1,1b       # spin until zero\n\t"
  "amoswap.w t1, t0, (%0)   # try for lock\n\t"
  "bnez t1,1b       # spin until we grabbed it\n\t"
  " # We hold the lock\n\t"
        :                       // no output regs
        : "r"(addr)             // input arg
        : "t0", "t1"            // clobbered temps
        );
  }

  inline void releaseLockASM(void* addr) {
    asm volatile (
  "amoswap.w x0,x0,(%0)  # release lock by storing 0\n\t"                
        :                       // no output regs
        : "r"(addr)             // input arg
        :                       // no clobbered temps
        );
  }
  
  class AtomicLock : public ShLock {
  public:
    // ShLock API
    void lock() override { acquireLock(); }
    void unlock() override { releaseLock(); }
    bool peekLock() const override { return mLock != 0u; }

    //
    AtomicLock()
      : mLock(0)
    {
      MFM_API_ASSERT_L1_ADDRESS(this);
    }
    
    bool tryLock() {
      return tryLockASM(&mLock);
    }

    void acquireLock() {
      acquireLockASM(&mLock);
    }

    void releaseLock() {
      releaseLockASM(&mLock);
    }

  private:
    u32 mLock;
  };

#if 0
  struct AtomicScopeLock {
    AtomicScopeLock(AtomicLock & lock)
      : mAtomicLock(lock)
    {
      mAtomicLock.acquireLock();
    }
    ~AtomicScopeLock() {
      mAtomicLock.releaseLock();
    }
    AtomicLock & mAtomicLock;
  };
#endif
}
