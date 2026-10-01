#pragma once    /* -*- C++ -*- */
#include "itype.h"
#include "Constants.h"
#include "AtomicLock.h"
#include "XUtils.h" // for memset_s
#include "PHASER.h" // for Phaser::CmdMask etc
#include "Debug.h"

namespace MFM {
  struct T6BoltResponder {
    AtomicLock mLock;
    u8 mLastSeqnoArrived;
    u8 mLastSeqnoAcked[HART_COUNT];
    u8 mLastSeqnoReturned;

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    bool isBoltAnyOfThese(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsAnyOf(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsNoneOf(PhaserBolt::CmdMask flags) ;

    void boltDetectorNC() ;

    void boltResponderAllHarts() ;

    // SERVICE ROUTINES
    bool hartAcknowledgeBolt() ;


  private:
    /// _methods ARE CALLED WITH mLock HELD!

    void _returnBoltToHostNC() ; 
    void _handleBlockingNC() ; 
  };

  extern T6BoltResponder theT6BoltResponder;
}
