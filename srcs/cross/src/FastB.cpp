#include "FastLocal.h"
#include "FastB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TransportBlock.h"
#include "EventWindow.h"

namespace MFM {
  struct FastB {
    EventWindow mFastEW;
  };
  FAST_LOCAL(FastB,fB,b);

  //  AtomicLock mylock;  // static -> can't hold shared locks in private RAM

  static int liveB(HostBlock & hb) {
    LOG.printf("%d:AWAITING EWS %d\n",fAll.mHartNum,estimateStackUsage());
    return 0;
  }

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    preloadT2Mailbox();
    DP.printf("B#%d(%d,%d)\n",hb.mTLBI,hb.mXPos,hb.mYPos);
    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    hb.mCommonArgs[1] = (u32) hb.mHostBaseAddrLo; //ET_NIU_BASE;
    hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;

    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveB(hb);          // go do your hart t2 thing you
    

#if 0
    if (false) {
      mylock.acquireLock();
      DP.printf("hart B: i hold the test lock 0x%x\n",hb.mCommonArgs[1]);
      if (!mylock.tryLock())
        DP.printf("hart B: and i can't take it again because i already have it doh\n");
      mylock.releaseLock();
      DP.printf("hart B: lock released [0x%08x]\n",createByMail());
    }
    return 0;
#endif
  }
}
