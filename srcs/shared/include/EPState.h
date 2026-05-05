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
    if (eps == EPState::ACTIVE) return "EPS:ACT";
    if (eps == EPState::CONFIGURED) return "EPS:CFG";
    if (eps == EPState::INITTED) return "EPS:INI";
    if (eps == EPState::UNINITTED) return "EPS:UNI";
    return "EPS:???";
  }
}

