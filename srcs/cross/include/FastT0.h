#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {
  extern int hartMainT0(HostBlock & hb) ;
  extern u32 totalMillisElapsed;     // given an assumed 'AI clock'
  extern u32 t0TicksElapsed;      // approximately 3-20Hz clock?

  inline u32 millisElapsed() { return totalMillisElapsed; }

  inline u32 millisFrom(u32 earlierms, u32 laterms) {
    return laterms - earlierms; // unsigned underflow 'just works'
  }
}
