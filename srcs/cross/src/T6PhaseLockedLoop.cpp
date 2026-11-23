#include "T6PhaseLockedLoop.h"
#include "T6Phaser.h"
#include "BlockCode.h"
#include "NRIUtils.h"
#include "T6ImageBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;
  
  // L1 DATA
  T6PhaseLockedLoop thePLL;

  const char * CmdArg::formatCmdArg() const {
    constexpr u32 BUF_SIZ = 34;
    static char buf[BUF_SIZ];
    snprintf(buf,BUF_SIZ,"%s:%u",
             PhaserBolt::phaserCmdName(mCmd),
             mArgs.mArg[0]);
    return buf;
  }

  void PhaseStepper::ConsensusChecker::setupVote(PBCmd cmd, Edge edge) {
    memset_s(this,'\0',sizeof(*this)); // all acks 0
    mPhase = cmd;
    mPhaseByte = PhaserBolt::phaserCmdName(cmd)[0];
    mEdge = edge;
    mEdgeByte = "<=>"[(u32) mEdge];
    for (u32 h = 0; h < HART_COUNT; ++h)
      mHartCharAcks[h] = UNK_CHAR;
    memoryFence();
    LOGPTAG(CCSUV,voteStatus());
  }

  void PhaseStepper::ConsensusChecker::rubberStamp() {
    if (mEdge == Edge::NONE) return;
    u8 hart = fAll.mHartNum;
    if (mHartCharAcks[hart] == ACK_CHAR) return;
    recordAck(mPhase,mEdge,hart);
    if (consensusReached()) LOGPTAG(OKAY,voteStatus());
  }

  void PhaseStepper::ConsensusChecker::recordAck(PBCmd cmd, Edge edge, u8 hart) {
    MFM_API_ASSERT(edge != Edge::NONE, ILLEGAL_ARGUMENT);
    MFM_API_ASSERT(hart < HART_COUNT, ILLEGAL_ARGUMENT);
    bool legal = mPhase == cmd && mEdge == edge;
    if (!legal) {
      LOGPTAG(RCKmp,(u32) cmd*100 + (u32) mPhase);
      LOGPTAG(RCKme,(u32) edge*100 + (u32) mEdge);
      MFM_API_ASSERT(legal, ILLEGAL_STATE); // return; //
    }
    mHartCharAcks[hart] = ACK_CHAR;
    memoryFence();
  }

  bool PhaseStepper::ConsensusChecker::consensusReached() {
    bool ret = true;
    for (u32 h = 0; h < HART_COUNT; ++h) {
      if (mHartCharAcks[h] == ACK_CHAR) continue;
      ret = false;
      break;
    }
    if (!ret) {
      EACH(10'000'000,LOGPTAG(CCBLOKT,voteStatus()));
    }
    return ret;
  }

  void PhaseStepper::_switchToEntering() {
    if (_exitingIfAny() == PBCmd::NOT_A_CMD) { // allowing switch to enter with no prev to get started..
      MFM_API_ASSERT(mIncomingCDA.mCmd != PBCmd::NOT_A_CMD, ILLEGAL_STATE); // as long as we have a place to go
    }
    mOutgoingCDA = NOTACmdArg;
    mCurrentCDA = mIncomingCDA;
    mVoteClerk.setupVote(mCurrentCDA.mCmd,Edge::ENTERING);
    HBPTAG(psSW2ENT,PhaserBolt::phaserCmdName(mCurrentCDA.mCmd));
  }

  void PhaseStepper::_switchToOccupying() {
    MFM_API_ASSERT(_enteringIfAny() != PBCmd::NOT_A_CMD, ILLEGAL_STATE); // entering before occupying
    mOutgoingCDA = NOTACmdArg;
    mCurrentCDA = mIncomingCDA;
    mIncomingCDA = NOTACmdArg;
    mVoteClerk.setupVote(mCurrentCDA.mCmd,Edge::NONE);
    HBPTAG(psSW2OCC,PhaserBolt::phaserCmdName(mCurrentCDA.mCmd));
  }

  void PhaseStepper::_initIncomingCmd(PBCmd nextCmd,const BoltArgs args) {
    MFM_API_ASSERT(mIncomingCDA.mCmd == PBCmd::NOT_A_CMD,ILLEGAL_STATE);    
    MFM_API_ASSERT(nextCmd != PBCmd::NOT_A_CMD,ILLEGAL_ARGUMENT);    
    mIncomingCDA.mCmd = nextCmd;
    mIncomingCDA.mArgs = args;
  }

  void PhaseStepper::_switchToExiting(PBCmd nextCmd,const BoltArgs args) {
    MFM_API_ASSERT(nextCmd != PBCmd::NOT_A_CMD,ILLEGAL_ARGUMENT);
    PBCmd ecmd = _occupyingIfAny();
    if (ecmd != PBCmd::NOT_A_CMD)                    // if we have real old cmd
      MFM_API_ASSERT(ecmd != nextCmd,ILLEGAL_STATE); // make sure we're changing

    mOutgoingCDA = mCurrentCDA;
    mIncomingCDA.mCmd = nextCmd;
    mIncomingCDA.mArgs = args;
    mVoteClerk.setupVote(mCurrentCDA.mCmd,Edge::EXITING);
    HBPTAG(psSW2EXI,PhaserBolt::phaserCmdName(mCurrentCDA.mCmd));
  }

  bool T6PhaseLockedLoop::_alerted() {
    if (fAll.mHartNum == HARTNUM_NC) 
      return _alertedNC();
    return mLastSeqnoReturned != mLastSeqnoArrived;
  }

  // only NC inspects the bolt directly..
  bool T6PhaseLockedLoop::_alertedNC() { 
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    if (!pay.isValid()) return true; // maybe?
    
    u8 pseq = pay.getSeqNo();
    return pseq != mLastSeqnoReturned;
  }

  void T6PhaseLockedLoop::phaseStepperControlNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    EACH(10'000'000,LOGPTAG(PHAZSC,__EACHNUM__));

    AtomicScopeLock guard(mLock);
    if (mPS._isExiting()) {
      if (mPS._consensusReached()) {
        HBPTAG(PSEXI,PhaserBolt::phaserCmdName(mPS._exitingIfAny()));
        mPS._switchToEntering();
      }

    } else if (mPS._isEntering()) {
      if (mPS._consensusReached()) {
        HBPTAG(PSENT,PhaserBolt::phaserCmdName(mPS._enteringIfAny()));
        mPS._switchToOccupying();
        if (_hartAcknowledgeBoltNC()) {
          HBPTAG(GOGOGO,mPS._voteStatus());
          HBPTAG(BOLTR_AHx,ackStatus());
        }
      }

    } else
      _boltDetectorNC();
  }

/*
  bool T6PhaseLockedLoop::ifEntering(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS._enteringIfAny() == cmd;
  }
*/

  bool T6PhaseLockedLoop::ifOccupying(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS._occupyingIfAny() == cmd;
  }

/*
  bool T6PhaseLockedLoop::ifExiting(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS._exitingIfAny() == cmd;
  }
*/

  void T6PhaseLockedLoop::switchToEntering() {
    EACH(1,HBPTAG(S2ENT,__EACHNUM__));
    AtomicScopeLock guard(mLock);
    mPS._switchToEntering();
  }

  void T6PhaseLockedLoop::switchToOccupying() {
    EACH(1,HBPTAG(S2ENT,__EACHNUM__));
    AtomicScopeLock guard(mLock);
    mPS._switchToOccupying();
  }

  void T6PhaseLockedLoop::switchToExiting(PBCmd nextCmd, const BoltArgs args) {
    EACH(1,HBPTAG(S2EWT,__EACHNUM__));
    AtomicScopeLock guard(mLock);
    mPS._switchToExiting(nextCmd,args);
  }

  void T6PhaseLockedLoop::continueWhenNotOccupying(PhaserBolt::CmdMask cmdset) {
    while (true) {
      AtomicScopeLock guard(mLock); // reacquire lock each iteration
      if (alerted()) return; // Alert means not occupying anything
      PBCmdMask occupyingMask = PhaserBolt::getCmdAsMask(mPS._occupyingIfAny());
      if (!(occupyingMask & cmdset)) return;
      sleepCycles(1'000'000);
    }
  }

  void __attribute__((weak)) assessProgressAllHarts() {
    FAIL(UNSUPPORTED_OPERATION);
  }
  
  char * T6PhaseLockedLoop::ackStatus() const {
    constexpr u32 BUF_SIZ = 16;
    static char buf[BUF_SIZ];
    snprintf(buf,BUF_SIZ,"V%u:A%u:R%u",
             mLastSeqnoArrived,
             mLastSeqnoAcked,
             mLastSeqnoReturned);
    return buf;
  }

/*
  bool T6PhaseLockedLoop::weHaveAcknowledgedCurrentBoltNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    AtomicScopeLock guard(mLock);
    return mLastSeqnoAcked == mLastSeqnoArrived;
  }
*/
  
  bool T6PhaseLockedLoop::_hartAcknowledgeBoltNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    if (mLastSeqnoArrived != mLastSeqnoAcked) {
      mLastSeqnoAcked = mLastSeqnoArrived;
      HBPTAG(NEWACK,mLastSeqnoAcked);
      return true;              // new ack
    }
    return false;               // already acked
  }

  void T6PhaseLockedLoop::_boltDetectorNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    //EACH(1'000'000,LOGPTAG(BOLTR_DET,__EACHNUM__));

    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    if (!pay.isValid()) return;

    EACH(10'000'000,LOGPTAG(BOLTR_VAL,__EACHNUM__));
    u8 pseq = pay.getSeqNo();
    if (pseq == mLastSeqnoReturned) return; // already fully handled

    EACH(1'000'000,LOGPTAG(BOLTR_SEQ,__EACHNUM__));
    if (pseq != mLastSeqnoArrived) { // new arrival
      LOGPTAG(CONN;SONAR,pay.formatPhaserBolt());

      MFM_API_ASSERT(!mPS._isExiting() && !mPS._isEntering(), ILLEGAL_STATE);
      PBCmd incoming = pay.getCmd();
      u32 argc = PhaserBolt::phaserArgCount(incoming);
      BoltArgs args = NOArgs;
      for (u32 i = 0; i < argc; ++i)
        args.mArg[i] = pay.getBoltDataWordOrDie(i);

      MFM_API_ASSERT(incoming != PBCmd::NOT_A_CMD,ILLEGAL_STATE);
      if (mNewestCmdReceived == PBCmd::NOT_A_CMD) { // if no OLD newest
        mPS._initIncomingCmd(incoming,args); // make this the incoming
        mPS._switchToEntering(); // and start entering it
      } else {
        mPS._switchToExiting(incoming,args); // starting to leave OLD, remember incoming
      }
      mNewestCmdReceived = incoming;
      mLastSeqnoArrived = pseq;
      return;
    }

    // still at station
    EACH(1'000,HBPTAG(BOLTR_WAIT,__EACHNUM__));
    if (pseq != mLastSeqnoAcked) {
      EACH(1'000'000,LOGPTAG(BOLTR_STILL,__EACHNUM__));
      return;
    }

    // ready to go
    HBPTAG(BOLTR_2ACK,pay.formatPhaserBolt());
    PBCmd pcmd = pay.getCmd();

    _returnBoltToHostNC();      // the bird is away
    mLastSeqnoReturned = pseq;  // admit we did it
    mNewestCmdReturned = pcmd;  // admit what we did
    _handleBlockingNC();        // but hang here (holding the lock!) if supposed to
  }

  void T6PhaseLockedLoop::_returnBoltToHostNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    //// SPIKE TO RETURN PHASER FIRE
    BlockCode destbc = BC_PHASER;
    u8 destbcindex = 0;

    // (0) find our own ImageBlockAddr for getSrcEPA().mBlockCode else bang
    // (1) find owniba.mHostChunkOffsetOpt != 255 or bang
    // (2) find u64 hostbaseaddr from hostblock lo,hi
    // (3) mDestBlockAddr = hostbaseaddr + 64*owniba.mHostChunkOffsetOpt
    ImageBlockHeader & ib = T6ImageBlock::getOurImageBlock();
    ImageBlockAddr iba = ib.findIBAIfAny(destbc);
    MFM_API_ASSERT(iba.isValid(),ILLEGAL_STATE); // (0)
    u8 hchunk = iba.getHostChunkOffsetOpt();
    MFM_API_ASSERT(hchunk!=255u,NO_MATCH); // (1)
    extern HostBlock theHostBlock;
    const HostBlock & hb = theHostBlock;
    u64 hostbaseaddr = hb.getOurHostNoCBaseAddress(); // (2)
    u64 destBlockAddr = hostbaseaddr + 64u * hchunk; // (3)
    u32 ourtlbi = hb.mTLBI;
    //LOGXTAG(RPHASElast,&lastPHASER[0]);
    //LOGPTAG64(RPHASEhbAddr,hostbaseaddr);
    //LOGPTAG64(RPHASEmDBAdr,destBlockAddr);

    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    LOGPTAG(BACK,pb.payload().formatPhaserBolt());
    u32 pbwordcount = sizeof(pb)>>2;
    s32 status = NRI3::initiateWriteToHost(hb.mNoC0,(u32*) &pb, pbwordcount, destBlockAddr);
    if (status != 1) LOGXTAG(RPHASEstatus,status);
  }

  void T6PhaseLockedLoop::_handleBlockingNC() { // mLock HELD
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    while (mPS._occupyingIfAny() == PBCmd::HOLD_AT_BIRTH) {
      if (_alertedNC()) return; // Alert means not occupying anything
      sleepCycles(1'000'000);
    }

  }


  
}
