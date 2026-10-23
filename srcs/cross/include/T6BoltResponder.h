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
    PhaserBolt::Cmd mNewestCmdReceived;
    PhaserBolt::Cmd mNewestCmdReturned;

    void init() {
      memset_s(this,'\0',sizeof(*this));
      mNewestCmdReceived = PhaserBolt::Cmd::NO_CMD;
      mNewestCmdReturned = PhaserBolt::Cmd::NO_CMD;
    }

    PhaserBolt::Cmd getInProgressCmdIfAny() ; //< mNewestCmdReceived if arr!=ret else no_cmd

    PhaserBolt::Cmd getPrevailingCmdIfAny() ; //< mNewestCmdReturned if arr==ret else no_cmd

    bool isBoltAnyOfThese(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsAnyOf(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsNoneOf(PhaserBolt::CmdMask flags) ;

    void boltDetectorNC() ;

    void boltResponderAllHarts() ;

    bool weHaveAcknowledgedCurrentBolt() ;
    
    // SERVICE ROUTINES
    bool hartAcknowledgeBolt() ;


  private:
    /// _methods ARE CALLED WITH mLock HELD!

    void _returnBoltToHostNC() ; 
    void _handleBlockingNC() ; 
  };

  extern T6BoltResponder theT6BoltResponder;

  /// 'IMAGE' routines to be defined in each image
  extern bool imageDoneRespondingToBoltB(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT0(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT1(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT2(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltNC(PhaserBolt & pb) ;
}
