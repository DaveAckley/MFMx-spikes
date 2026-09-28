#include "T6BoltResponder.h"
#include "T6Phaser.h"

namespace MFM {
  // L1 DATA
  T6BoltResponder theT6BoltResponder;

  void T6BoltResponder::boltDetectorNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    AtomicScopeLock guard(mLock);
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    HBNOTE(BOLTR_DET);
    LOGNOTE(__FUNCTION__);
  }
}
