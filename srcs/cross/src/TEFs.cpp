#include "TEFs.h"

namespace MFM {
  TEFResult TaskEpochFunction_BOOT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    MFM_API_ASSERT_ON_HART(hartnum);
    HBPTAG(BOOT@,getHartEpochName(hei));
    return TEFR_CONTINUE;
  }

}
