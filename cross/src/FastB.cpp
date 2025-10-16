#include "FastLocal.h"
#include "FastB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"

namespace MFM {
  struct FastB {
  };

  AtomicLock mylock;  // static -> can't hold shared locks in private RAM

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    preloadT2Mailbox();
    DP.printf("HI from %d (%d,%d)\n",hb.mTLBI,hb.mXPos,hb.mYPos);
    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    hb.mCommonArgs[1] = (u32) hb.mHostBaseAddrLo; //ET_NIU_BASE;
    hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;

    mylock.acquireLock();
    DP.printf("hart B: i hold the test lock 0x%x\n",hb.mCommonArgs[1]);
    if (!mylock.tryLock())
      DP.printf("hart B: and i can't take it again because i already have it doh\n");
    mylock.releaseLock();
    DP.printf("hart B: lock released [0x%08x]\n",createByMail());
    return 0;
  }
}
