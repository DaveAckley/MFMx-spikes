#include "PhaseSteppers.h"  //< for theL1CacheLoaders
#include "T6PhaseLockedLoop.h"

namespace MFM {
  namespace PhaseSteppers {

    extern "C" void assessProgressAllHarts() {
      thePLL.rubberStamp();
    }
  }
}

