#include "T6PhaseLockedLoop.h"
#include "T6Phaser.h"
#include "BlockCode.h"
#include "NRIUtils.h"
#include "T6ImageBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;
  
  // L1 DATA
  T6PhaseLockedLoop thePS;

  void PhaseStepper::_switchToEntering() {
    PBCmd ecmd = exitingIfAny();
    MFM_API_ASSERT(ecmd != PBCmd::NOT_A_CMD,ILLEGAL_STATE);
    mOutgoingCmd = PBCmd::NOT_A_CMD;
    mCurrentCmd = mIncomingCmd;
  }

  void PhaseStepper::_switchToExiting(PBCmd nextCmd) {
    MFM_API_ASSERT(nextCmd != PBCmd::NOT_A_CMD,ILLEGAL_ARGUMENT);
    PBCmd ecmd = occupyingIfAny();
    MFM_API_ASSERT(ecmd != PBCmd::NOT_A_CMD,ILLEGAL_STATE);
    MFM_API_ASSERT(ecmd != nextCmd,ILLEGAL_STATE);

    mOutgoingCmd = mCurrentCmd;
    mIncomingCmd = nextCmd;
  }

#if 0
  PBCmd T6PhaseLockedLoop::getInProgressCmdIfAny() {
    AtomicScopeLock guard(mLock);
    if (mLastSeqnoArrived == mLastSeqnoReturned)
      return PhaserBolt::Cmd::NOT_A_CMD; // nothing in progress
    return mNewestCmdReceived; // or here it is
  }

  PBCmd T6PhaseLockedLoop::getPrevailingCmdIfAny() {
    AtomicScopeLock guard(mLock);
    if (mLastSeqnoArrived != mLastSeqnoReturned)
      return PhaserBolt::Cmd::NOT_A_CMD; // nothing prevailing
    return mNewestCmdReturned; // or here it is
  }
#endif

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
    /*
     if (exiting in progress) check exiting
     else if (entering in progress) check entering
     else if (new bolt detection)
          check command
    */
    AtomicScopeLock guard(mLock);
    if (mPS.isExiting()) {
      FAIL(INCOMPLETE_CODE); /// CHECK EXITING PROGRESS
    } else if (mPS.isEntering()) {
      FAIL(INCOMPLETE_CODE); /// CHECK ENTERING PROGRESS
    } else
      _boltDetectorNC();
  }

  bool T6PhaseLockedLoop::ifEntering(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS.enteringIfAny() == cmd;
  }

  bool T6PhaseLockedLoop::ifOccupying(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS.occupyingIfAny() == cmd;
  }

  bool T6PhaseLockedLoop::ifExiting(PBCmd cmd) {
    AtomicScopeLock guard(mLock);
    return mPS.exitingIfAny() == cmd;
  }

  void T6PhaseLockedLoop::continueWhenNotOccupying(PhaserBolt::CmdMask cmdset) {
    while (true) {
      AtomicScopeLock guard(mLock); // reacquire lock each iteration
      if (alerted()) return; // Alert means not occupying anything
      PBCmdMask occupyingMask = PhaserBolt::getCmdAsMask(mPS.occupyingIfAny());
      if (!(occupyingMask & cmdset)) return;
      sleepCycles(1'000'000);
    }
  }


  void T6PhaseLockedLoop::boltResponderAllHarts() {
    EACH(100'000'000,LOGPTAG(BOLTR_AHS,__EACHNUM__));

    // RACY RACY NO LOCK
    if (mLastSeqnoAcked[fAll.mHartNum] == mLastSeqnoArrived) return; // we have already acked this

    PhaserBlock & block = T6Phaser::getPhaserBlock();
    PhaserBolt & pb = block.payload();

    switch (fAll.mHartNum) {
    case HARTNUM_B:
      if (!imageDoneRespondingToBoltB(pb)) return;
      break;
    case HARTNUM_T0:
      if (!imageDoneRespondingToBoltT0(pb)) return;
      break;
    case HARTNUM_T1:
      if (!imageDoneRespondingToBoltT1(pb)) return;
      break;
    case HARTNUM_T2:
      if (!imageDoneRespondingToBoltT2(pb)) return;
      break;
    case HARTNUM_NC:
      if (!imageDoneRespondingToBoltNC(pb)) return;
      break;
    default:
      FAIL(UNREACHABLE_CODE);
    }
    //XXX if (!crosslibReadyToAckBolt(pb)) return;

    if (hartAcknowledgeBolt())
      EACH(139,LOGPTAG(BOLTR_AHx,(u32) mLastSeqnoAcked[fAll.mHartNum]));
  }

  bool T6PhaseLockedLoop::weHaveAcknowledgedCurrentBolt() {
    AtomicScopeLock guard(mLock);
    return mLastSeqnoAcked[fAll.mHartNum] == mLastSeqnoArrived;
  }
  
  bool T6PhaseLockedLoop::hartAcknowledgeBolt() {
    AtomicScopeLock guard(mLock);

    if (mLastSeqnoArrived != mLastSeqnoAcked[fAll.mHartNum]) {
      mLastSeqnoAcked[fAll.mHartNum] = mLastSeqnoArrived;
      return true;              // new ack
    }
    return false;               // already acked
  }

  void T6PhaseLockedLoop::_boltDetectorNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    EACH(10'000'000,LOGPTAG(BOLTR_DET,__EACHNUM__));

    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    if (!pay.isValid()) return;
    
    EACH(10'000'000,LOGPTAG(BOLTR_VAL,__EACHNUM__));
    u8 pseq = pay.getSeqNo();
    if (pseq == mLastSeqnoReturned) return; // already fully handled

    EACH(1'000'000,LOGPTAG(BOLTR_SEQ,__EACHNUM__));
    if (pseq != mLastSeqnoArrived) { // new arrival
      mLastSeqnoArrived = pseq;
      mNewestCmdReceived = pay.getCmd();
      LOGPTAG(BOLTR_NEW,formatPhaserBolt(pay));
      return;
    }

    // still at station
    EACH(100'000,LOGPTAG(BOLTR_WAIT,__EACHNUM__));
    for (u32 h = HARTNUM_B; h < HART_COUNT; ++h) {
      if (pseq != mLastSeqnoAcked[h]) {
        EACH(1'000'000,LOGPTAG(BOLTR_STILL,__EACHNUM__));
        return;
      }
    }

    // ready to go
    LOGPTAG(BOLTRDONE,formatPhaserBolt(pay));
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
    LOGPTAG(BOLTBACK,pb.payload().getSeqNo());
    u32 pbwordcount = sizeof(pb)>>2;
    s32 status = NRI3::initiateWriteToHost(hb.mNoC0,(u32*) &pb, pbwordcount, destBlockAddr);
    if (status != 1) LOGXTAG(RPHASEstatus,status);
  }

  void T6PhaseLockedLoop::_handleBlockingNC() { // mLock HELD
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    continueWhenNotOccupying(PhaserBolt::CmdMask::HOLD_AT_BIRTH); // only blocks all harts left
  }


  
}
