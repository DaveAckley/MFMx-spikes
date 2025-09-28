/* -*- C++ -*- */
#pragma once

#include "P2PElevator.h"

namespace MFM {
  struct Block1K {
    char mData[1<<10];
  };
  struct TransportBlock {
    TransportBlock() { }
    typedef P2PElevatorPlatform<Block1K,3> ThreeBlocks;
    ThreeBlocks mPlatform;
  };
}
