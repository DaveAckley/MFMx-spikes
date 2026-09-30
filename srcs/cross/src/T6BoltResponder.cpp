#include "T6BoltResponder.h"
#include "T6Phaser.h"
#include "BlockCode.h"
#include "NRIUtils.h"
#include "T6ImageBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;
  
  // L1 DATA
  T6BoltResponder theT6BoltResponder;

  void T6BoltResponder::boltDetectorNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    EACH(10'000'000,LOGPTAG(BOLTR_DET,__EACHNUM__));

    AtomicScopeLock guard(mLock);
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    if (!pay.isValid()) return;
    
    EACH(10'000'000,LOGPTAG(BOLTR_VAL,__EACHNUM__));
    u8 pseq = pay.getSeqNo();
    if (pseq == mLastSeqnoReturned) return; // already fully handled

    EACH(100'000,LOGPTAG(BOLTR_SEQ,__EACHNUM__));
    if (pseq != mLastSeqnoArrived) { // new arrival
      LOGPTAG(BOLTR_NEW,pseq);
      mLastSeqnoArrived = pseq;
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
    LOGPTAG(BOLTR_READY,PhaserBolt::phaserCmdName(pay.getCmd()));

    _returnBoltToHostNC();      // the bird is away
    mLastSeqnoReturned = pseq;  // admit we're done
    _handleBlockingNC();        // but hang here (holding the lock!) if supposed to
  }

  bool T6BoltResponder::shallWeCarryOn() {
    // RACY RACY NO LOCK
    if (mLastSeqnoReturned != mLastSeqnoArrived) return false;
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    return pay.isValid() && (pay.getCmd() == PhaserBolt::CMD_CARRY_ON);
  }

  void T6BoltResponder::boltResponderAllHarts() {
    EACH(1'000'000,LOGPTAG(BOLTR_AHS,__EACHNUM__));
    // XXX JUST ACK FOR NOW
    if (hartAcknowledgeBolt())
      LOGPTAG(BOLTR_AH,(u32) mLastSeqnoAcked[fAll.mHartNum]);
  }

  bool T6BoltResponder::hartAcknowledgeBolt() {
    AtomicScopeLock guard(mLock);

    if (mLastSeqnoArrived != mLastSeqnoAcked[fAll.mHartNum]) {
      mLastSeqnoAcked[fAll.mHartNum] = mLastSeqnoArrived;
      return true;              // new ack
    }
    return false;               // already acked
  }

  void T6BoltResponder::_returnBoltToHostNC() {
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
    LOGPTAG64(RPHASEhbAddr,hostbaseaddr);
    LOGPTAG64(RPHASEmDBAdr,destBlockAddr);
    LOGPTAG(RPHASEtlbi,ourtlbi);

    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    u32 pbwordcount = sizeof(pb)>>2;
    s32 status = NRI3::initiateWriteToHost(hb.mNoC0,(u32*) &pb, pbwordcount, destBlockAddr);
    LOGXTAG(RPHASEstatus,status);
  }

  void T6BoltResponder::_handleBlockingNC() { // mLock HELD
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    PhaserBolt & pay = pb.payload();
    u8 pseq = mLastSeqnoReturned;
    {
      while (pseq == pay.getSeqNo() && pay.getCmd() == PhaserBolt::CMD_ALL_HARTS_PAUSE) {
        EACH(100'000,HBNOTE(PHASEPAUSE));
        sleepCycles(100'000);
      }
    }
    {
      HostBlock & hb = theHostBlock;
      while (pseq == pay.getSeqNo() && pay.getCmd() == PhaserBolt::CMD_HOLD_AT_BIRTH &&
             hb.mPerHartStatus[fAll.mHartNum] == FAILCode::LIVING) {
        EACH(100'000,HBNOTE(PHASEHOLD));
        sleepCycles(100'000);
      }
    }
  }

  
}
