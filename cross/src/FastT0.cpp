#include "FastLocal.h"
#include "FastT0.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"

namespace MFM {
  struct FastT0 {
    u8 honk[321];
  };
  FAST_LOCAL(FastT0,fT0,t0);

  int hartMainT0(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    preloadT2Mailbox();
    DP.printf("%d:HI![%d] ",fAll.mHartNum,++fT0.honk[117]);
    return 0;
  }
}
