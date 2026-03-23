#include "ZotBlock.h"

namespace MFM {
  TCBase * ZotEP::getCarPtrIfAny(u8 carindex) const {
#ifndef BUILD_HOST      
    {
      static u32 once;
      if (once<5) {
        extern HostBlock theHostBlock;
        theHostBlock.addBytes(':',hartChar(fAll.mHartNum));
        theHostBlock.addBytes('0'+carindex,hartChar(fAll.mHartNum));
        once++;
      }
    }
#endif
    if (carindex >= CAR_COUNT) return 0;
    MFM_API_ASSERT_NONNULL(mCarStgPtr);
    return mCarStgPtr->getCarPtr(carindex);
  }
}
