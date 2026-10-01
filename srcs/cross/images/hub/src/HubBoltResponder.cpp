#include "T6BoltResponder.h"
#include "EP_InterHub.h"  //< for InterHubL1Control

namespace MFM {
  /// HUB BOLT RESPONDERS

  bool imageDoneRespondingToBoltB(PhaserBolt & pb) {
    if (pb.getCmd() == PhaserBolt::CMD_NEW_LEADER) {
      s32 arg;
      if (pb.getBoltDataWordIfAny(0,arg) && arg >= 0 && arg < 256) {
        LOGPTAG(BOLTR_NEWLEADER,arg);
        InterHubL1Control & ihlc = theInterHubL1Control;
        AtomicScopeLock guard(ihlc.mIHL1Lock);
        ihlc.mSuperCycleLeader = (u8) arg;
        ihlc.mSuperCycleState = 0;   // whatever that means.
      } else 
        FAIL(OUT_OF_BOUNDS);
    }
    return true; // done either way.
  }
  bool imageDoneRespondingToBoltT0(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltT1(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltT2(PhaserBolt & pb) { return true; }
  bool imageDoneRespondingToBoltNC(PhaserBolt & pb) { return true; }
}
