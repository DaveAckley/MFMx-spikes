/* -*- C++ -*- */

#include "TaskWorker.h"
#include "BlockCode.h"
#include "T6Phaser.h"
#include "T6ImageBlock.h"
#include "NRIUtils.h"
#include "Debug.h"

namespace MFM {
  template <class IMGTM>
  typename TaskManager<IMGTM>::TaskXFerRB & TaskManager<IMGTM>::getXFerRBOrDie(u32 hartNumFrom, u32 hartNumTo) {
    TaskXFerRB * ptr = getXFerRBIfAny(hartNumFrom, hartNumTo);
    MFM_API_ASSERT_NONNULL(ptr);
    return *ptr;
  }

  template <class IMGTM>
  s8 TaskManager<IMGTM>::checkPhaserDispatch() {
    static u32 lastSeqNo = U32_MAX;
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    EACH(10'000'000,LOGPTAG(NCPB,lastSeqNo));
    if (pb.isComplete()) {
      PhaserBolt & pay = pb.payload();
      EACH(10'000'000,LOGPTAG(NCPBCOMPx,&pay));
      if (pay.isValid()) {
        u8 seqno = pay.getSeqNo();
        EACH(10'000'000,LOGPTAG(NCPBCOMPs,(u32) seqno));
        if (lastSeqNo != seqno) {
          EACH(1,LOGPTAG(TKMG_NCPBCOMP,pay.getSeqNo()));
          LOGPX(lastSeqNo);
          LOGPX(seqno);
          //// HANDLE PHASER BOLT
          bool respond = true;  // assume we'll shoot it back
          if (pay.getCmd() == PhaserBolt::CMD_LOOP_BACK) {
            LOGPTAG(NCPB_LOOP_BACK_MAN!,seqno);
          } else if (pay.getCmd() == PhaserBolt::CMD_SPIKE_PING) {
            s32 x, y;
            pay.getBoltDataWordIfAny(0,x);
            pay.getBoltDataWordIfAny(1,y);
            S8C dest(x,y);
            LOGPTAG(SPIKEPINGDEST!,dest);
#if 0
            // SPIKE: TRY TO MAKE A TASK FOR HB
            TaskManager::TaskXFerRB & n2brb = getXFerRBOrDie(HARTNUM_NC, HARTNUM_B);
            if (n2brb.isFull()) {
              LOGPTAG(TKMG_NOROOMn2b,dest);
              return 1; // retvalsayswhat?
            }
            u8 tn = TaskWorker::createTask(Task::TTYPE_IHPPING);
            if (tn == TaskCommon::TASK_NUMBER_NONE) {
              LOGPTAG(TKMG_NOROOMtasks,dest);
              return 1; // retvalsayswhat?
            }
            Task & t = TaskWorker::getTask(tn);
            t.mBArg1 = (u8) dest.x;
            t.mBArg2 = (u8) dest.y;
            //n2brb.add(tn);
            LOGPTAG(TKMG_2UHB,tn);
#endif            
          } else {
            LOGPTAG(CALLDOWN,PhaserBolt::phaserCmdName(pay.getCmd()));
            s8 ret = maybeHandleBolt(pay, lastSeqNo); //< CALL DOWN TO IMAGE
            LOGPTAG(PAYCMD,pay.getCmd());
            if (ret == 0) return 0;
            if (ret < 0) respond = false;
            // else respond = true;
          }
          //// RESPOND
          lastSeqNo = seqno;
          LOGPTAG(NCPB_LASTSEQNO,lastSeqNo);
          if (!respond) return 0;
          
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
          s32 status = NRI3::initiateWriteToHost(hb.mNoC0,(u32*) &pb, sizeof(pb), destBlockAddr);
          //s32 status = 0x8787;
          LOGXTAG(RPHASEstatus,status);
          return 0;
        }
      }
    }
    return -1; // retvalsezwhat
  }
}
