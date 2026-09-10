#pragma once         /* -*- C++ -*- */
#include "PHASER.h"
#include "CrossUtils.h" // for memoryFence

extern "C" void * addrPHASERBlock(); // in _BUD.S

namespace MFM {
  struct T6Phaser {
    static PhaserBlock & getPhaserBlock() {
      memoryFence();
      PhaserBlock * pb = (PhaserBlock*) addrPHASERBlock();
      MFM_API_ASSERT_NONNULL(pb);
      return *pb;
    }

    static void handle() ;

  };
}


