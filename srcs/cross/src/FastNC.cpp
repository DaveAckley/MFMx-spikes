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
      {
    ImageBlockHeader * ibh = (ImageBlockHeader*) 0x14; // WELL-KNOWN ADDRESS GAH
    extern HostBlock theHostBlock;
    HostBlock & hb = theHostBlock;
    hb.addBytes('\\','\\');
    hb.packString(getNameFromImageCode((ImageCode) ibh->getImageCode()));
    hb.addBytes('/','/');
      }
      EPFuncPtr* start_addr = (EPFuncPtr*) &__start_rodata_fp_table_nc;
      EPFuncPtr* end_addr = (EPFuncPtr*) &__end_rodata_fp_table_nc;

#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," 2COPE 0x%p-0x%p\n", start_addr, end_addr);
          theHostBlock.packString(buf);
        }
      }
#endif

      u32 ptrCount = end_addr - start_addr;
      MFM_API_ASSERT(ptrCount < MAX_EPFUNCS,OUT_OF_ROOM);
      for (u32 i = 0u; i < ptrCount; ++i) {
        mEPFuncs[i] = start_addr[i];
#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," copEED %lu @0x%p = 0x%p\n", i, &mEPFuncs[i], mEPFuncs[i]);
          theHostBlock.packString(buf);
        }
      }
#endif
      }

      mEPFuncsInUse = (u8) ptrCount;
    }

    bool runEPFuncsNC(bool forInit) {
#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," runEPF %u %u\n",
                       forInit, mEPFuncsInUse);
          theHostBlock.packString(buf);
        }
      }
#endif

      bool ret = false;
      for (u32 i = 0u; i < mEPFuncsInUse; ++i) {
        EPFuncPtr epf = mEPFuncs[i];
        if (epf) {
#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," %lu run@0x%p\n",
                       i, epf);
          theHostBlock.packString(buf);
        }
      }
#endif
      if ((*epf)(forInit)) {
#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," %lu bongn@0x%p\n",
                       i, mEPFuncs[i]);
          theHostBlock.packString(buf);
        }
      }
#endif
        ret = true;
      }
        }
      }
#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," aNOut %u %u\n",
                       forInit, mEPFuncsInUse);
          theHostBlock.packString(buf);
        }
      }
#endif

      return ret;
    }

  };
  FAST_LOCAL(FastNC,fNC,n);

  int stepNC(HostBlock & hb) {
    fNC.runEPFuncsNC(false);
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    fNC.init();

#ifndef BUILD_HOST      
      {
        static u32 once = 0;
        if (++once<5u) {
          extern HostBlock theHostBlock;
          char buf[100];
          npf_snprintf(buf,100," %s:%d: haro\n",
                       __FILE__,__LINE__);
          theHostBlock.packString(buf);
        }
      }
#endif

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    return liveNC(hb);

    return 0; /* NOT REACHED */
  }
}

