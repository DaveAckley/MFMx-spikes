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

  static int liveNC(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveNC(HostBlock & hb) {
    u8 spin = 0u;
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      theT6ElevatorTransport.updateTransportBlock();
      hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
      sleepCycles(1'000u);
    }
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    theT6ElevatorTransport.init(hb,theTransportBlock);

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveNC(hb);

    return 0; /* NOT REACHED */
  }
}

