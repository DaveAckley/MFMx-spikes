/* -*- C++ -*- */

namespace MFM {
  template <class IMGTM>
  s8 TaskManager<IMGTM>::checkPhaser() {
    static u32 lastSeqNo = U32_MAX;
    PhaserBlock & pb = T6Phaser::getPhaserBlock();
    EACH(1'000'000,LOGPTAG(NCPB,lastSeqNo));
    if (pb.isComplete()) {
      PhaserBolt & pay = pb.payload();
      if (pay.isValid()) {
        u8 seqno = pay.getSeqNo();
        if (lastSeqNo != seqno) {
          EACH(1,LOGPTAG(TKMG_NCPBCOMP,pay.getSeqNo()));
          LOGPX(lastSeqNo);
          LOGPX(seqno);
          //// HANDLE PHASER BOLT
          if (pay.getCmd() == PhaserBolt::CMD_SPIKE_PING) {
            s32 x, y;
            pay.getBoltDataWordIfAny(0,x);
            pay.getBoltDataWordIfAny(1,y);
            S8C dest(x,y);
            LOGPTAG(SPIKEPINGDEST!,dest);
            // SPIKE: TRY TO MAKE A TASK FOR HB
#if 0 // how to query for an xferrb?
            TaskManager::TaskXFerRB & n2brb = theTaskManager.getXFerRB(HARTNUM_NC, HARTNUM_B);
            if (n2brb.isFull()) {
              LOGPTAG(TKMG_NOROOMn2b,dest);
              return 1; // retvalsayswhat?
            }
#endif
            u8 tn = TaskWorker::createTask(Task::TTYPE_IHPPING);
            if (tn == TaskCommon::TASK_NUMBER_NONE) {
              LOGPTAG(TKMG_NOROOMtasks,dest);
              return 1; // retvalsayswhat?
            }
            Task & t = TaskWorker::getTask(tn);
            t.mBArg1 = (u8) dest.x;
            t.mBArg2 = (u8) dest.y;
            n2brb.add(tn);
            LOGPTAG(TKMG_2UHB,tn);
          } else {
            LOGPTAG(PAYCMD,pay.getCmd());
          }
          //// RESPOND
          lastSeqNo = seqno;
          
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
        }
      }
    }
    return -1; // retvalsezwhat
  }
}
