#pragma once  /* -*- C++ -*- */
#include "Random.h"

namespace MFM {

  struct HostRandom : public Random {
    HostRandom()
      : Random((u32) time(NULL))
    { }
  };
  extern thread_local HostRandom hostPRNG;
}

