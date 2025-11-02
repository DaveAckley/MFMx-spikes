#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "P4Atom.h"

namespace MFM {
  struct EventWindow {
    static constexpr u8 ATOM_COUNT = 41u;
    P4Atom mAtoms[ATOM_COUNT];
    void reset() {
      memset_s(&mAtoms[0],0u,sizeof(mAtoms));
    }
  };
}

