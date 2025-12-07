#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"
#include "T6RingO.h"

namespace MFM {
  struct FastT1 {
    T6RingOscillators mRingOs;
  };

  FAST_LOCAL(FastT1,fT1,t1);

  static int liveT1(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveT1(HostBlock & hb) {
    volatile u8 spin = 0u;
    DP.printf("T1lvie(%d,%d)\n",fAll.mPos.x,fAll.mPos.y);
    fT1.mRingOs.init();
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      fT1.mRingOs.update();
    }
    return 0;
  }

  int hartMainT1(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    DP.printf("%s:Hoo[0x%04x] ",hartName(fAll.mHartNum), createBits(16));
    //    LOG.printf("BANGYORDED\n");
    //    LOG.printf("LOGAT(%p)\n",&LOG);
    u32 stop = createBits(2u)/*+1u*/;
    for (u32 i = 0u; i < stop; ++i) {
      LOG.printf("HIO %02d %02d T1@(%u,%u) SEZ '%c'!\n",
                 create(41),
                 create(41),
                 hb.mPos.x,hb.mPos.y,
                 createBits(6)+32u);
      sleepCycles(100'000u);
    }
    LOG.printf("%s: %d MAXSTAX\n",hartName(fAll.mHartNum),estimateStackUsage());
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT1(hb);          // go do your hart t1 thing you
  }  
}
