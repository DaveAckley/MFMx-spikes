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
    for (u32 i = 0u; i < 20u; ++i)
      LOG.printf("HI LOOK T6/T1@(%u,%u) SENT YOU '%c'!\n",
                 hb.mXPos,hb.mYPos,createBits(6)+32u);
    return 0;
  }  
}
