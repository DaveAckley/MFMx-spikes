#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "AtomicLock.h"

namespace MFM {
  class DebugPrint {
  public:
    u32 printf(const char * format, ...);
  private:
    AtomicLock mLock;
  };

  extern DebugPrint DP;
}
