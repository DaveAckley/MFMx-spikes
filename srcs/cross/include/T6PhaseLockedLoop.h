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

  enum class Edge : u8 {
    ENTERING,
    EXITING
  };

  struct ConsensusChecker {
    PBCmd mPhase;
    Edge mEdge;
    u8 mHartCharAcks[HART_COUNT+1];
    static constexpr u8 ACK_CHAR = 'A';
    static constexpr u8 UNK_CHAR = '.';

    void setupVote(PBCmd cmd, Edge edge) ;
    void recordAck(PBCmd cmd, Edge edge, u8 hart) ;
    const char * voteStatus() { return (const char *) mHartCharAcks; };

    bool consensusReached() ;
  };

  struct PhaseStepper {

    bool isExiting() const { return exitingIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd exitingIfAny() const {
      if (mOutgoingCmd == mCurrentCmd && mCurrentCmd != mIncomingCmd) 
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }

    bool isEntering() const { return enteringIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd enteringIfAny() const {
      if (mOutgoingCmd != mCurrentCmd && mCurrentCmd == mIncomingCmd)
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }
    PBCmd occupyingIfAny() const {
      if (mOutgoingCmd == PBCmd::NOT_A_CMD && mIncomingCmd == PBCmd::NOT_A_CMD)
        return mCurrentCmd;
      return PBCmd::NOT_A_CMD;
    }

    void _switchToEntering(ConsensusChecker & cc) ; // exiting -> entering

    void _switchToExiting(ConsensusChecker & cc, PBCmd nextCmd) ; // occupying -> exiting

    //bool _hartAcked() { return mHartAckFlags & _thisHartFlag(); }
    //u8 _thisHartFlag() const { return (u8) (1u<<fAll.mHartNum); }

  private:
    PBCmd mOutgoingCmd;
    PBCmd mCurrentCmd;
    PBCmd mIncomingCmd;
    //    u8 mHartAckFlags;
  };

  struct T6PhaseLockedLoop {

    AtomicLock mLock;
    u8 mLastSeqnoArrived;
    u8 mLastSeqnoAcked[HART_COUNT];
    u8 mLastSeqnoReturned;
    PBCmd mNewestCmdReceived;
    PBCmd mNewestCmdReturned;

    PhaseStepper mPS;
    ConsensusChecker mVoteClerk;

    void init() {
      memset_s(this,'\0',sizeof(*this));
      /* NOT_A_CMD necessarily == 0
      mNewestCmdReceived = PhaserBolt::Cmd::NOT_A_CMD;
      mNewestCmdReturned = PhaserBolt::Cmd::NOT_A_CMD;
      */
    }

    void continueWhenNotOccupying(PhaserBolt::CmdMask flags) ;

    void switchToEntering() ;

    void switchToExiting(PBCmd nextCmd) ;

    bool ifEntering(PBCmd cmd) ;

    bool ifOccupying(PBCmd cmd) ;

    bool ifExiting(PBCmd cmd) ;

    void phaseStepperControlNC() ;

    bool alerted() {
      AtomicScopeLock guard(mLock);
      return _alerted();
    }

    void boltResponderAllHarts() ;

    void doneEntering(PBCmd cmd) {
      mVoteClerk.recordAck(cmd,Edge::ENTERING,fAll.mHartNum);
    }

    void doneExiting(PBCmd cmd) {
      mVoteClerk.recordAck(cmd,Edge::EXITING,fAll.mHartNum);
    }

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

  extern T6PhaseLockedLoop thePLL; // the PhaseStepper instance (and stuf)

  /// 'IMAGE' routines to be defined in each image
  extern bool imageDoneRespondingToBoltB(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT0(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT1(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT2(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltNC(PhaserBolt & pb) ;
}
