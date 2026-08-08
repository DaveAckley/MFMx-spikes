#include "TEFs.h"

namespace MFM {
  TEFResult TaskEpochFunction_BOOT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    MFM_API_ASSERT_ON_HART(hartnum); // doh 
    if (hartnum == HARTNUM_B && hei == HE_BGN) {
      SNAP(5,HBPTAG(BOOT@,getHartEpochName(hei)));
      return TEFR_CONTINUE;
    }
    return TEFR_NO_THANKS;      // nobody's doing anything here right now
  }

}
