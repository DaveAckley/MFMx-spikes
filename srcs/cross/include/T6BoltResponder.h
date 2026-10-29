#pragma once    /* -*- C++ -*- */
#include "itype.h"
#include "Constants.h"
#include "AtomicLock.h"
#include "XUtils.h" // for memset_s
#include "PHASER.h" // for Phaser::CmdMask etc
#include "Debug.h"
#include "FastLocal.h" // for fALL

namespace MFM {
  using PBCmd = PhaserBolt::Cmd;
  using PBCmdMask = PhaserBolt::CmdMask;

  struct PhaseStepper {

    bool isExiting() const { return _exitingIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd _exitingIfAny() const {
      if (mOutgoingCmd == mCurrentCmd && mCurrentCmd != mIncomingCmd) 
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }

    bool isEntering() const { return _enteringIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd _enteringIfAny() const {
      if (mOutgoingCmd != mCurrentCmd && mCurrentCmd == mIncomingCmd)
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }
    PBCmd _occupyingIfAny() const {
      if (mOutgoingCmd == PBCmd::NOT_A_CMD && mIncomingCmd == PBCmd::NOT_A_CMD)
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }

    void _switchToEntering() ; // exiting -> entering

    void _switchToExiting(PBCmd nextCmd) ; // occupying -> exiting

    bool _hartAcked() { return mHartAckFlags & _thisHartFlag(); }
    u8 _thisHartFlag() const { return (u8) (1u<<fAll.mHartNum); }

  private:
    PBCmd mOutgoingCmd;
    PBCmd mCurrentCmd;
    PBCmd mIncomingCmd;
    u8 mHartAckFlags;
  };

  struct T6BoltResponder {
    AtomicLock mLock;
    u8 mLastSeqnoArrived;
    u8 mLastSeqnoAcked[HART_COUNT];
    u8 mLastSeqnoReturned;
    PBCmd mNewestCmdReceived;
    PBCmd mNewestCmdReturned;

    PhaseStepper mPS;

    void init() {
      memset_s(this,'\0',sizeof(*this));
      /* NOT_A_CMD necessarily == 0
      mNewestCmdReceived = PhaserBolt::Cmd::NOT_A_CMD;
      mNewestCmdReturned = PhaserBolt::Cmd::NOT_A_CMD;
      */
    }

    void continueWhenNotOccupying(PhaserBolt::CmdMask flags) ;

#if 0
    PhaserBolt::Cmd getInProgressCmdIfAny() ; //< mNewestCmdReceived if arr!=ret else no_cmd

    PhaserBolt::Cmd getPrevailingCmdIfAny() ; //< mNewestCmdReturned if arr==ret else no_cmd

    bool isBoltAnyOfThese(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsAnyOf(PhaserBolt::CmdMask flags) ;

    void continueWhenBoltIsNoneOf(PhaserBolt::CmdMask flags) ;

#endif
    void phaseStepperControlNC() ;

    bool alerted() {
      AtomicScopeLock guard(mLock);
      return _alerted();
    }

    void boltResponderAllHarts() ;

    bool weHaveAcknowledgedCurrentBolt() ;
    
    // SERVICE ROUTINES
    bool hartAcknowledgeBolt() ;

  private:
    /// _methods ARE CALLED WITH mLock HELD!

    bool _alerted() ;
    bool _alertedNC() ;
    void _boltDetectorNC() ;
    void _returnBoltToHostNC() ; 
    void _handleBlockingNC() ; 
  };

  extern T6BoltResponder thePS; // the PhaseStepper instance (and stuf)

  /// 'IMAGE' routines to be defined in each image
  extern bool imageDoneRespondingToBoltB(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT0(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT1(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT2(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltNC(PhaserBolt & pb) ;
}
