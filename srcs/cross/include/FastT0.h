#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {
  extern int hartMainT0(HostBlock & hb) ;
  extern u32 totalMillisElapsed;     // given an assumed 'AI clock'
  extern u32 t0TicksElapsed;         // approximately 3-20Hz clock? (Sun Dec 21 02:11:33 2025 now ~25-150Hz)

  inline u32 millisElapsed() { return totalMillisElapsed; }

  inline u32 millisFrom(u32 earlierms, u32 laterms) {
    return laterms - earlierms; // unsigned underflow 'just works'
  }
}
