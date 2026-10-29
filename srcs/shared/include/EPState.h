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
    if (eps == EPState::ACTIVE) return "ES:AC";
    if (eps == EPState::CONFIGURED) return "ES:CF";
    if (eps == EPState::INITTED) return "ES:IN";
    if (eps == EPState::UNINITTED) return "ES:UN";
    return "EPS:???";
  }
}

