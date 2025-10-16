#include "FastNC.h"
#include "FastLocal.h"
#include "TransportBlock.h"
#include "Printf.h"

namespace MFM {
  T6ElevatorTransport theT6ElevatorTransport;

  LogCarStorage theLogCarStorage __attribute__ ((section(".transportblocklog")));
  EWCarStorage theEWCarStorage __attribute__ ((section(".transportblockew")));

  TransportBlock theTransportBlock __attribute__ ((section(".transportblock"))) = {
    .mLogCarStorageT6Ptr = (u32) &theLogCarStorage,
    .mEWCarStorageT6Ptr = (u32) &theEWCarStorage,
  };

  struct FastNC {

  };
  FAST_LOCAL(FastNC,fNC,nc);

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    theT6ElevatorTransport.init(hb,theTransportBlock);
    //DP.printf("NC:HILO:0x%08lx%08lx\n",hb.mHostBaseAddrHi,hb.mHostBaseAddrLo);

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    while (true)
      theT6ElevatorTransport.updateTransportBlock();
    return 0; /* NOT REACHED */
  }
}

