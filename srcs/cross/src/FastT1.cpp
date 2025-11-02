#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"

namespace MFM {
  struct FastT1 {
    u8 clams[23];
  };

  FAST_LOCAL(FastT1,fT1,t1);

  int hartMainT1(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    DP.printf("%d:Hoo[0x%04x] ",fAll.mHartNum, createBits(16));
    //    LOG.printf("BANGYORDED\n");
    //    LOG.printf("LOGAT(%p)\n",&LOG);
    u32 stop = createBits(3u)+3u;
    for (u32 i = 0u; i < stop; ++i) {
      LOG.printf("HI LOOK %02d %02d T1@(%u,%u) SENT YOU '%c'!\n",
                 create(41),
                 create(41),
                 hb.mXPos,hb.mYPos,
                 createBits(6)+32u);
      sleepCycles(100'000'000u);
    }
    LOG.printf("%d:GO LIVE MAXSTAX %d\n",fAll.mHartNum,estimateStackUsage());
    //    FAIL(USER_REQUESTED_FAILURE); // try to set T1's fail bit
    return 0;
  }  
}
