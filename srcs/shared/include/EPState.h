#pragma once  /* -*- C++ -*- */
#include "itype.h"

namespace MFM {
  enum EPState : u8 {
    UNINITTED = 0,
    INITTED,
    CONFIGURED,
    ACTIVE
  };

  constexpr const char * getNameFromEPState(EPState eps) {
    if (eps == EPState::ACTIVE) return "ACTV";
    if (eps == EPState::CONFIGURED) return "CNFG";
    if (eps == EPState::INITTED) return "INIT";
    if (eps == EPState::UNINITTED) return "UNIN";
    return "?EPS?";
  }
}

