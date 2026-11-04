#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {

  extern u32 millisElapsed() ;
  extern u32 ticksElapsed() ;

  inline u32 millisFrom(u32 earlierms, u32 laterms) {
    return laterms - earlierms; // unsigned underflow 'just works'
  }

  inline bool millisLess(u32 less, u32 more) {
    return millisFrom(less,more) < millisFrom(more,less);
  }
}
