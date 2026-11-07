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
    NONE,
    EXITING
  };

  extern "C" void assessProgressAllHarts() ;

  struct BoltArgs {
    static constexpr u32 MAX_ARGS = 2;
    s32 mArg[MAX_ARGS];
  };
  struct CmdArg {
    PBCmd mCmd;
    BoltArgs mArgs;
    const char * formatCmdArg() const ;
  };
  static constexpr BoltArgs NOArgs = { 0, 0 };
  static constexpr CmdArg NOTACmdArg = { PBCmd::NOT_A_CMD, NOArgs };

  struct PhaseStepper {

    bool _isExiting() const { return _exitingIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd _exitingIfAny() const {
      if (mOutgoingCDA.mCmd == mCurrentCDA.mCmd && mCurrentCDA.mCmd != mIncomingCDA.mCmd) 
        return mCurrentCDA.mCmd;
      return PBCmd::NOT_A_CMD;
    }

    bool _isEntering() const { return _enteringIfAny() != PBCmd::NOT_A_CMD; }
    PBCmd _enteringIfAny() const {
      if (mOutgoingCDA.mCmd != mCurrentCDA.mCmd && mCurrentCDA.mCmd == mIncomingCDA.mCmd)
        return mCurrentCDA.mCmd;
      return PBCmd::NOT_A_CMD;
    }
    PBCmd _occupyingIfAny() const {
      if (mOutgoingCDA.mCmd == PBCmd::NOT_A_CMD && mIncomingCDA.mCmd == PBCmd::NOT_A_CMD)
        return mCurrentCDA.mCmd;
      return PBCmd::NOT_A_CMD;
    }

    void _initIncomingCmd(PBCmd nextCmd,const BoltArgs args) ;

    void _switchToEntering() ; // exiting -> entering

    void _switchToOccupying() ; // entering -> occupying

    void _switchToExiting(PBCmd nextCmd,const BoltArgs args) ; // occupying -> exiting

    void _rubberStamp() { mVoteClerk.rubberStamp(); }

    void _recordAck(PBCmd cmd, Edge edge, u8 hart) { mVoteClerk.recordAck(cmd,edge,hart); }

    bool _ifHartCouldAck(PBCmd cmd, Edge edge, u8 hart) {
      return mVoteClerk.ifHartCouldAck(cmd,edge,hart);
    }

    const char * _voteStatus() { return mVoteClerk.voteStatus(); }

    bool _consensusReached() { return mVoteClerk.consensusReached(); }

    CmdArg _getOutgoingCDA() const { return mOutgoingCDA; }
    CmdArg _getCurrentCDA() const { return mCurrentCDA; }
    CmdArg _getIncomingCDA() const { return mIncomingCDA; }

  private:

    struct ConsensusChecker {
      PBCmd mPhase;
      Edge mEdge;
      u8 mPhaseByte;                  //<CONTIGUOUS
      u8 mEdgeByte;                   //<CONTIGUOUS
      u8 mHartCharAcks[HART_COUNT+1]; //<CONTIGUOUS
      static constexpr u8 ACK_CHAR = 'A';
      static constexpr u8 UNK_CHAR = '.';

      void rubberStamp() ;
      void setupVote(PBCmd cmd, Edge edge) ;
      void recordAck(PBCmd cmd, Edge edge, u8 hart) ;
      const char * voteStatus() { return (const char *) &mPhaseByte; };

      bool hartAcked(u8 hart) const { // phase and step UNSPECIFIED!
        return
          hart < HART_COUNT &&
          mHartCharAcks[hart] == ACK_CHAR;
      }

      bool ifHartCouldAck(PBCmd cmd, Edge edge, u8 hart) const {
        return
          cmd == mPhase &&
          edge == mEdge &&
          !hartAcked(hart);
      }

      bool consensusReached() ;
    };

    ConsensusChecker mVoteClerk;

    CmdArg mOutgoingCDA;
    CmdArg mCurrentCDA;
    CmdArg mIncomingCDA;
  };

  struct T6PhaseLockedLoop {

    AtomicLock mLock;
    u8 mLastSeqnoArrived;
    u8 mLastSeqnoAcked;
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

    CmdArg getOutgoingCDA() {
      AtomicScopeLock guard(mLock);
      return mPS._getOutgoingCDA();
    }
    CmdArg getCurrentCDA() {
      AtomicScopeLock guard(mLock);
      return mPS._getCurrentCDA();
    }
    CmdArg getIncomingCDA() {
      AtomicScopeLock guard(mLock);
      return mPS._getIncomingCDA();
    }

    bool ifCouldAck(PBCmd cmd, Edge edge) {
      AtomicScopeLock guard(mLock);
      return mPS._ifHartCouldAck(cmd,edge,fAll.mHartNum);
    }

    void continueWhenNotOccupying(PhaserBolt::CmdMask flags) ;

    char * ackStatus() const ; // TOTALLY RACY

    const char * voteStatus() {
      AtomicScopeLock guard(mLock);
      return mPS._voteStatus();
    }

    void switchToEntering() ;

    void switchToOccupying() ;

    void switchToExiting(PBCmd nextCmd, const BoltArgs args) ;

    //bool ifEntering(PBCmd cmd) ;

    bool ifOccupying(PBCmd cmd) ;

    //bool ifExiting(PBCmd cmd) ;

    void phaseStepperControlNC() ;

    bool alerted() {
      AtomicScopeLock guard(mLock);
      return _alerted();
    }

    void rubberStamp() { //< approve WevTF is going on
      AtomicScopeLock guard(mLock);
      mPS._rubberStamp();
    }

    void ackEntering(PBCmd cmd) {
      AtomicScopeLock guard(mLock);
      mPS._recordAck(cmd,Edge::ENTERING,fAll.mHartNum);
    }

    void ackExiting(PBCmd cmd) {
      AtomicScopeLock guard(mLock);
      mPS._recordAck(cmd,Edge::EXITING,fAll.mHartNum);
    }

    // SERVICE ROUTINES
    bool _hartAcknowledgeBoltNC() ;

  private:
    /// _methods ARE CALLED WITH mLock HELD!

    bool _alerted() ;
    bool _alertedNC() ;
    void _boltDetectorNC() ;
    void _returnBoltToHostNC() ; 
    void _handleBlockingNC() ; 
  };

  extern "C" void assessProgressAllHarts() ; //< weak in T6PhaseLockedLoop.cpp

  extern T6PhaseLockedLoop thePLL; // the PhaseStepper instance (and stuf)

  /// 'IMAGE' routines to be defined in each image
  extern bool imageDoneRespondingToBoltB(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT0(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT1(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltT2(PhaserBolt & pb) ;
  extern bool imageDoneRespondingToBoltNC(PhaserBolt & pb) ;
}
