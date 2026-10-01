#include "T6BoltResponder.h"

namespace MFM {
  /// ZOT BOLT RESPONDERS

  // XXX FOR STARTERS WE DON'T CARE ABOUT ANY BOLTS
  bool imageReadyToAckBoltB(PhaserBlock & pb) { return true; }
  bool imageReadyToAckBoltT0(PhaserBlock & pb) { return true; }
  bool imageReadyToAckBoltT1(PhaserBlock & pb) { return true; }
  bool imageReadyToAckBoltT2(PhaserBlock & pb) { return true; }
  bool imageReadyToAckBoltNC(PhaserBlock & pb) { return true; }
}
