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
    bool swap(u32 sn1, u32 sn2) {
      if (sn1 >= ATOM_COUNT ||
          sn2 >= ATOM_COUNT)
        return false;
      
      P4Atom a1 = mAtoms[sn1];
      P4Atom a2 = mAtoms[sn2];
      if (a1.getType() == P4Atom::INACCESSIBLE_TYPE ||
          a2.getType() == P4Atom::INACCESSIBLE_TYPE)
        return false;
      mAtoms[sn1] = a2;
      mAtoms[sn2] = a1;
      return true;
    }
  };
}

