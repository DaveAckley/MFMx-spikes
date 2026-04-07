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
//#include "FastT2.h" // REMEMBER: NO createByMail on HART NC!

namespace MFM {

  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    return liveNC(hb);

    return 0; /* NOT REACHED */
  }
}

