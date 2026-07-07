#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"
#include "Debug.h"
#include "DefaultLives.h"
#include "EP_LogBlock.h" // for theLogBlockL1Control

namespace MFM {

  int initT1() {
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    HBMARK;
    LOGMARK;
    return 0;
  }

  /*
  int stepT1(HostBlock & hb) {
    //    HBPTAG(LB1Ct,&theLogBlockL1Control);
    return theLogBlockL1Control.step(hb);
  }
  */

  int hartMainT1(HostBlock & hb) {
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT1(hb);          // go do your hart t1 thing you
  }  
}
