#include "StandardLife.h"
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
#include "T6Phaser.h"
#include "T6BoltResponder.h"

namespace MFM {

  volatile PhaserBlock lastPHASER[1];

  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  extern "C" char __start_rodata_fp_table_nc[];
  extern "C" char __end_rodata_fp_table_nc[];

  struct FastNC {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyHTFuncsNC();
    }

    u64 mBytesOut, mBytesIn;

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

  void stepNC(HostBlock & hb) {
    theT6BoltResponder.boltDetectorNC();
    RCFlag res = fNC.runHTFuncsNC(HTOpCode::HTOC_LIVE);
    EACH(10'000'000,LOGPTAG64(NCBO,fNC.mBytesOut));
  }

  int initNC() {
    HBNOTE(BOLTI_INIT);
    theT6BoltResponder.init();
    HBNOTE("initNC");
    fNC.init();
    return 0;
  }

  /*
  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    HBPTAG(@,__FUNCTION__);
    return liveNC(hb);
  }
  */

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

