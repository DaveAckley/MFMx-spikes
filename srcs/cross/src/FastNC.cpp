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

namespace MFM {

  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  extern "C" char __start_rodata_fp_table_nc[];
  extern "C" char __end_rodata_fp_table_nc[];

  struct FastNC {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyEPFuncsNC();
      runEPFuncsNC(true);
    }

    static constexpr u32 MAX_EPFUNCS = 6u;
    EPFuncPtr mEPFuncs[MAX_EPFUNCS];
    u8 mEPFuncsInUse;

    void copyEPFuncsNC() {
      HBMARK;

      EPFuncPtr* start_addr = (EPFuncPtr*) &__start_rodata_fp_table_nc;
      EPFuncPtr* end_addr = (EPFuncPtr*) &__end_rodata_fp_table_nc;

      //HBPTAG(ncstart,start_addr);
      //HBPTAG(ncend,end_addr);

      u32 ptrCount = end_addr - start_addr;
      HBPTAG(ptrC,ptrCount);
      MFM_API_ASSERT(ptrCount < MAX_EPFUNCS,OUT_OF_ROOM);
      for (u32 i = 0u; i < ptrCount; ++i) {
        mEPFuncs[i] = start_addr[i];
        //HBPTAG(copEED,(void*) mEPFuncs[i]);
      }

      mEPFuncsInUse = (u8) ptrCount;
    }

    bool runEPFuncsNC(bool forInit) {
      //if (forInit) HBPTAG(runEPFNC,forInit);
      bool ret = false;
      for (u32 j = 0u; j < mEPFuncsInUse; ++j) {
        //        u32 i = mEPFuncsInUse-j-1u; // DEBUG: RUN INITS BACKWARDS
        u32 i = j;
        EPFuncPtr epf = mEPFuncs[i];
        if (epf) {
          if (forInit) HBPTAG(fncInit,i); 
          //if (forInit) HBPTAG(fncPtr,(void*) epf);
          //if (!forInit) HBPTAG(4STEPNC,i);
          if ((*epf)(forInit)) {
            if (forInit) HBPTAG(true,(void*) epf);
            ret = true;
          }
        }
      }
      if (forInit) HBPTAG(epfInUse,mEPFuncsInUse);
      return ret;
    }
  };
  FAST_LOCAL(FastNC,fNC,n);

  int stepNC(HostBlock & hb) {
    if (!fNC.runEPFuncsNC(false))
      breathe();
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
    return liveNC(hb);
  }
}

