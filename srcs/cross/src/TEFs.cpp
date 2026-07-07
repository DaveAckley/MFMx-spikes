#include "TEFs.h"

namespace MFM {
  TEFResult TaskEpochFunction_BOOT(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    MFM_API_ASSERT_ON_HART(hartnum);
    HBNOTE(EARLY BOOT);
    return TEFR_CONTINUE;
  }

}
