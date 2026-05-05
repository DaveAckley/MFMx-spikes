#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"
#include "Debug.h"
#include "DefaultLives.h"

namespace MFM {
  struct FastT1 {
    //T6RingOscillators mRingOs;
  };

  FAST_LOCAL(FastT1,fT1,1);

  int liveT1(HostBlock & hb) /*__attribute__ ((optimize("O2"))*/;

  int liveT1(HostBlock & hb) {
    u64 spin = 0u;
    //    HBMARK;

    while (true) {
      ++spin;
      if ((spin % (1<<5u))==0) hb.hartbeat(fAll.mHartNum);
    }
    return 0;
  }

  int initT1() {
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    return 0;
  }

  int hartMainT1(HostBlock & hb) {
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    return liveT1(hb);          // go do your hart t1 thing you
  }  
}
