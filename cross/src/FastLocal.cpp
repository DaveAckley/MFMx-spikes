#include "FastLocal.h"
#include "FastB.h"
#include "FastT0.h"
#include "FastT1.h"
#include "FastT2.h"
#include "FastNC.h"

namespace MFM {
  // Everybody gets their own copy of FastAll
  FAST_LOCAL(FastAll,fAll,all);

  void sleepCycles(u32 cycles) {
    volatile u32 reg = cycles|1;
    while (--reg != 0u) { }
  }
}

