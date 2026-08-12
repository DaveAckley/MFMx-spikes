#include "DefaultLives.h"
#include "FastNC.h"
#include "FastLocal.h"
#include "TC.h"
#include "CellBlock.h"
//#include "T6Grid.h"
#include "Printf.h"
#include "CrossUtils.h"
//#include "NoCs.h"
#include "S8C.h"
#include "nanoprintf.h"
#include "ImageBlock.h"
#include "Debug.h"
//#include "FastT2.h" // REMEMBER: NO createByMail on HART NC!
#include "HartTasksLib.h" // for HTFuncPtr
#include "BlockCode.h"
#include "T6ImageBlock.h"

extern "C" unsigned readPHASER() ; // In _BUD.S

namespace MFM {

  u32 lastPHASER[1];

  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  extern "C" char __start_rodata_fp_table_nc[];
  extern "C" char __end_rodata_fp_table_nc[];

  struct FastNC {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyHTFuncsNC();
#if 0
      mPrivSeq.copyHTFuncs(HARTNUM_NC,
                           (HTFuncPtr*) &__start_rodata_fp_table_nc,
                           (HTFuncPtr*) &__end_rodata_fp_table_nc);
      mPrivSeq.runHTFuncs(HTOpCode::HTOC_INIT);
#endif
    }

    u64 mBytesOut, mBytesIn;

#if 0    
    PrivateSequencer mPrivSeq;
#endif

    static constexpr u32 MAX_EPFUNCS = 6u;
    HTFuncPtr mHTFuncs[MAX_EPFUNCS];
    u8 mHTFuncsInUse;


    void copyHTFuncsNC() {
      //HBMARK;
      LOGMARK;

      HTFuncPtr* start_addr = (HTFuncPtr*) &__start_rodata_fp_table_nc;
      HTFuncPtr* end_addr = (HTFuncPtr*) &__end_rodata_fp_table_nc;

      //HBPTAG(ncstart,start_addr);
      //HBPTAG(ncend,end_addr);

      u32 ptrCount = end_addr - start_addr;
      LOGPTAG(ptrC,ptrCount);
      MFM_API_ASSERT(ptrCount < MAX_EPFUNCS,OUT_OF_ROOM);
      for (u32 i = 0u; i < ptrCount; ++i) {
        mHTFuncs[i] = start_addr[i];
        //HBPTAG(copEED,(void*) mHTFuncs[i]);
      }

      mHTFuncsInUse = (u8) ptrCount;
    }

    RCFlag runHTFuncsNC(HTOpCode htoc) {
      if (htoc==HTOpCode::HTOC_LIVE)
        SNAP(2,HBPTAG(@,__FUNCTION__));
      else if (htoc==HTOpCode::HTOC_INIT)
        HBPTAG(init@,__FUNCTION__);
      else if (htoc==HTOpCode::HTOC_OPEN)
        HBPTAG(open@,__FUNCTION__);
      else FAIL(UNREACHABLE_CODE);
      for (u32 j = 0u; j < mHTFuncsInUse; ++j) {
        u32 i = j;
        HTFuncPtr epf = mHTFuncs[i];
        if (epf) {
          (*epf)(htoc);
        }
      }
      return RCFlag::RC_ZERO;
    }
  };
  FAST_LOCAL(FastNC,fNC,n);

  u64 recordBytesOINC(bool out, u32 count) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    if (out) return fNC.mBytesOut += count;
    return fNC.mBytesIn += count;
  }

  int stepNC(HostBlock & hb) {
    static u32 spin;
    u32 PHASER = readPHASER();
    if (PHASER != lastPHASER[0]) {
      lastPHASER[0] = PHASER;
      LOGXX(PHASER);
      HBXX(PHASER);
      //// SPIKE TO RETURN PHASER FIRE
      if (true) {
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
        HBPTAG(RPHASEhchunk,hchunk);
        HBXTAG64(RPHASEhbAddr,hostbaseaddr);
        HBXTAG64(RPHASEmDBAdr,destBlockAddr);
        HBXTAG(RPHASEtlbi,ourtlbi);
        s32 status = NRI3::initiateWriteToHost(hb.mNoC0,(u32*) &lastPHASER[0], 1, destBlockAddr);
        if (status == 0) HBXTAG(RPHASEFAILSHIPHOST,lastPHASER[0]);
      }
        
    }

    RCFlag res = fNC.runHTFuncsNC(HTOpCode::HTOC_LIVE);
    if ((++spin & 0xf'ffff) == 0) {
      HBXTAG(stepNCing,spin);
      LOGXX(spin);
      LOGPTAG64(NCBO,fNC.mBytesOut);
      //      LOGPTAG64(NCBI,fNC.mBytesIn);
    }
    return 0;
  }

  int initNC() {
    HBNOTE("initNC");
    fNC.init();
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    HBPTAG(@,__FUNCTION__);
    return liveNC(hb);
  }

  ////////
  TEFResult TaskEpochFunction_NOC(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    if (hartnum!=HARTNUM_NC) return TEFR_NO_THANKS;

    switch (hei) {
    case HE_BGN:
      HBNOTE(init NOC);
      initNC();
      return TEFR_CONTINUE;

    case HE_BORN0:
      HBMARK;
      return TEFR_CONTINUE;

    case HE_BORN1:
      HBMARK;
      fNC.runHTFuncsNC(HTOC_INIT);
      return TEFR_CONTINUE;

    case HE_GROW0:
      HBMARK;
      fNC.runHTFuncsNC(HTOC_OPEN);
      return TEFR_CONTINUE;

    case HE_GROW2:
      return TEFR_HART_OUT;     // DONE

    default:
      break;
    }
    return TEFR_NO_THANKS;
  }

}

