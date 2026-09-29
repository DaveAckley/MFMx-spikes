#include "StandardLife.h"
#include "HubLiveB.h"
#include "ExtraConstants.h"
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for preloadT2Mailbox
#include "AtomicLock.h"
#include "EventWindow.h"
#include "EwpBlock.h"
#include "EP_ACacheBlock.h"
#include "T6Phaser.h"
#include "TaskWorker.h"

namespace MFM {

  FAST_LOCAL(FastB,fB,b);

  // L1 DATA
  T6Grid theT6Grid[1];
  DLGridList theDLGridList;
  L1GridManagerControl theL1GridManagerControl;

  /*
  bool processInterHubCars(u32 ngbidx, HostBlock & hb,bool inside) {
    fB.mPIHControl.updateCars(ngbidx,hb,inside);
    EACH(1'000'000,HBPTAG(ihPROCAR,&theInterHubL1Data.getCarStg(ngbidx)));
    return false; // ??
  }
  */

  int myInitB() {
    HBPTAG(INIT+,fAll.mNoC0);
    theDLGridList.init();
    theL1GridManagerControl.init();
    fB.mGridManager.init(theT6Grid[0],theACacheBlockL1Control,theDLGridList);

    theInterHubL1Control.init();
    fB.mPIHControl.init(theInterHubL1Control);

    HBPTAG(INIT-,fAll.mNoC0);
    theHostBlock.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // entering event loop
    HBMARK;
    LOGMARK;
    return 0;
  }

  int stepB(HostBlock & hb) {
    static u32 spin = 0u;

    const u32 BITS = 16;//15;
    const u32 LIM = (1<<BITS)-1;
    if ((++spin & LIM) == 0) {
      HBPTAG(horg,spin>>BITS); // generate some HB logging please?
    }
    if ((spin & 0x3ff) == 0)
      hb.hartbeat(fAll.mHartNum);

    fB.mPIHControl.stepB(hb);

    return 0;
  }
}
