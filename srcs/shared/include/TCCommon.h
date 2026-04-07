/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"
#include "TCMarker.h"

#ifndef BUILD_HOST      
#include "HostBlock.h"
#include "nanoprintf.h"
#endif

namespace MFM {

  struct TCOpsData {
    u32 mArrivalTime;           // in whatever units host vs cross
    u32 mDepartureTime;         // ditto
  };

  union TCWord {
    TCMarker mMarker;
    u32 mWord;
    u8 mBytes[4];

    TCWord() { mWord = 0u; }
    TCWord(const TCWord & other) { mWord = other.mWord; }
  };

}
