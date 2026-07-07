#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "UxC.h" // for U16C
#include "lzmfmx.h"
#include "AtomicLock.h"

#include <thread> // for thread
#include <memory> // for unique_ptr
#include <atomic> // for atomic

namespace MFM {

  struct ZHostDecompressor {
    ZHostDecompressor() ;
    void init() ;
    lzmfmx mLZ;

    std::unique_ptr<std::thread> mDecompressorThreadPtr;
    std::atomic<bool> mDecompressorThreadAlive;
    AtomicLock mDecompressorThreadMutex;
  };
}




  
