#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"
#include "Debug.h"
#include "DefaultLives.h"
#include "EP_LogBlock.h" // for theLogBlockL1Control
#include "HartTasksLib.h" // for HTFuncPtr

namespace MFM {

#if 0  /* don't run htfuncs on T1? */

  struct FastT1 {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyHTFuncsT1();
      runHTFuncsT1(true);
    }

    static constexpr u32 MAX_HTFUNCS_T1 = 3u;
    HTFuncPtr mHTFuncsT1[MAX_HTFUNCS_T1];
    u8 mHTFuncsInUseT1;

    void copyHTFuncsT1() {
      LOGMARK;

      HTFuncPtr* start_addr = (HTFuncPtr*) &__start_rodata_fp_table_t1;
      HTFuncPtr* end_addr = (HTFuncPtr*) &__end_rodata_fp_table_t1;

      u32 ptrCount = end_addr - start_addr;
      HBPTAG(ptrC,ptrCount);
      MFM_API_ASSERT(ptrCount < MAX_HTFUNCS_T1,OUT_OF_ROOM);
      for (u32 i = 0u; i < ptrCount; ++i) 
        mHTFuncsT1[i] = start_addr[i];
      mHTFuncsInUseT1 = (u8) ptrCount;
    }

    bool runHTFuncsT1(bool forInit) {
      bool ret = false;
      for (u32 j = 0u; j < mHTFuncsInUseT1; ++j) {
        u32 i = j;
        HTFuncPtr epf = mHTFuncsT1[i];
        if (epf) {
          if (forInit) HBPTAG(fncInit,i); 
          if ((*epf)(forInit)) 
            ret = true;
        }
      }
      if (forInit) HBPTAG(epfInUse,mHTFuncsInUseT1);
      return ret;
    }
  };
  FAST_LOCAL(FastT1,fT1,1);

  int initT1() {
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    HBMARK;
    fT1.init();
    LOGMARK;
    return 0;
  }

  int stepT1(HostBlock & hb) {
    fT1.runHTFuncsT1(false);
    return 0;
  }

#endif
  
  int hartMainT1(HostBlock & hb) {
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    //    HBPTAG(@,__FUNCTION__);
    return liveT1(hb);          // go do your hart t1 thing you
  }  
}
