#pragma once  /* -*- C++ -*- */
#include "Random.h"

namespace MFM {

  struct HostRandom : public Random {
    using result_type = u32;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return U32_MAX; }
    result_type operator()() { return Create(); }
    
    HostRandom()
      : Random((u32) time(NULL))
    { }
  };
  extern thread_local HostRandom hostPRNG;
}

