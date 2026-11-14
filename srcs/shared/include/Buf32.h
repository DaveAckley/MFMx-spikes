#pragma once  /* -*- C++ -*- */

#include "itype.h"

namespace MFM { 
  struct Buf32 {  // Make Buf32 available to all
    static constexpr u8 SIZE = 32;
    char buf[SIZE];
  };
}
