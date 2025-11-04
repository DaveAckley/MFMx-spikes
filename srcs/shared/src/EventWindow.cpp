#include "EventWindow.h"

namespace MFM {
  bool EventWindow::swap(u32 sn1, sn2) {
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
}
