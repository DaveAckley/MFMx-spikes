#include "RunLevel.h"

namespace MFM {
  void PrivateSequencer::copyHTFuncs(u8 onhartnum, HTFuncPtr* rodata_start_addr, HTFuncPtr* rodata_end_addr) {
    MFM_API_ASSERT_ON_HART(onhartnum);
    u32 ptrCount = rodata_end_addr - rodata_start_addr;
    HBPTAG(ptrC,ptrCount);
    MFM_API_ASSERT(ptrCount < MAX_EPFUNCS,OUT_OF_ROOM);
    for (u32 i = 0u; i < ptrCount; ++i) {
        mHTFuncs[i] = start_addr[i];
    }
    mHTFuncsInUse = (u8) ptrCount;
    mOnHartNum = onhartnum;
  }

  void PrivateSequencer::runHTFuncs(HTOpCode htoc) {
    MFM_API_ASSERT_ON_HART(mOnHartNum);
    for (u32 j = 0u; j < mHTFuncsInUse; ++j) {
      u32 i = j;
      HTFuncPtr epf = mHTFuncs[i];
      if (epf) {
        if (forInit) LOGPTAG(fncInit,i); 
        (*epf)(htoc);
      }
    }
  }
}
