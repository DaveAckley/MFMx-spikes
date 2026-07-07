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
#include "HartTasks.h" // for HTFuncPtr

namespace MFM {

  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  extern "C" char __start_rodata_fp_table_nc[];
  extern "C" char __end_rodata_fp_table_nc[];

  struct FastNC {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyHTFuncsNC();
      runHTFuncsNC(true);
    }

    static constexpr u32 MAX_EPFUNCS = 6u;
    HTFuncPtr mHTFuncs[MAX_EPFUNCS];
    u8 mHTFuncsInUse;

    void copyHTFuncsNC() {
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

    bool runHTFuncsNC(bool forInit) {
      //if (forInit) HBPTAG(runEPFNC,forInit);
      bool ret = false;
      for (u32 j = 0u; j < mHTFuncsInUse; ++j) {
        //        u32 i = mHTFuncsInUse-j-1u; // DEBUG: RUN INITS BACKWARDS
        u32 i = j;
        HTFuncPtr epf = mHTFuncs[i];
        if (epf) {
          if (forInit) LOGPTAG(fncInit,i); 
          //if (forInit) HBPTAG(fncPtr,(void*) epf);
          //if (!forInit) HBPTAG(4STEPNC,i);
          if ((*epf)(forInit)) {
            if (forInit) LOGPTAG(true,(void*) epf);
            ret = true;
          }
        }
      }
      if (forInit) LOGPTAG(epfInUse,mHTFuncsInUse);
      return ret;
    }
  };
  FAST_LOCAL(FastNC,fNC,n);

  int stepNC(HostBlock & hb) {
    if (!fNC.runHTFuncsNC(false))
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

