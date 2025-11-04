#pragma once /* -*- C++ -*- */

#include <chrono>

namespace MFM {
  typedef std::chrono::time_point<std::chrono::steady_clock> TimeStamp;
  typedef TimeStamp::duration TimeDuration;
  typedef std::chrono::microseconds TimeDurationMicros;
}

