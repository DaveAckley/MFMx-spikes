#include "T6BoltResponder.h"
#include "T6Phaser.h"

namespace MFM {
  // L1 DATA
  T6BoltResponder theT6BoltResponder;

  void T6BoltResponder::boltDetectorNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    AtomicScopeLock guard(mLock);
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    if (!pay.isValid()) return;
    
    u8 pseq = pay.getSeqNo();
    if (pseq == mLastSeqnoReturned) return; // already fully handled

    if (pseq != mLastSeqnoArrived) { // new arrival
      HBPTAG(BOLTR_NEW,pseq);
      mLastSeqnoArrived = pseq;
      return;
    }

    // still at station
    for (u32 h = HARTNUM_B; h < HART_COUNT; ++h) {
      if (pseq != mLastSeqnoAcked[h]) {
        EACH(1'000'000,LOGPTAG(BOLTR_STILL,__EACHNUM__));
        return;
      }
    }

    // ready to go
      

  }

  bool T6BoltResponder::hartAcknowledgeBolt() {
    if (mLastSeqnoArrived != mLastSeqnoAcked[fAll.mHartNum]) {
      mLastSeqnoAcked[fAll.mHartNum] = mLastSeqnoArrived;
      return true;              // new ack
    }
    return false;               // already acked
  }
}
