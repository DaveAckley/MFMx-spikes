#pragma once  /* -*- C++ -*- */
#include <thread>
#include <functional>
#include "Random.h"

namespace MFM {

  struct HostRandom : public Random {
    using result_type = u32;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return U32_MAX; }
    result_type operator()() { return Create(); }
    
    HostRandom()
      : Random(std::hash<std::thread::id>{}(std::this_thread::get_id()) // urg ai
               + (u32) time(NULL))
    { }
  };
  extern thread_local HostRandom hostPRNG;
}

