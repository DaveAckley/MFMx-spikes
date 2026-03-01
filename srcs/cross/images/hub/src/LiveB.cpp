#include "LiveB.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "TransportBlock.h"
#include "EventWindow.h"
#include "T6Grid.h"
#include "T6CellO.h"
#include "H2EEP.h"
#include "NRIUtils.h" // for NRI3

#include "AllImageBlockDecls.h"
#include "ImageConfig.h"        // for theImageBlock

#include "T6STVL.h" // XXX TESTING

namespace MFM {

  extern int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  T6Grid theT6Grid[1] __attribute__ ((section(".crossrodata")));

  static constexpr u32 EWSLOTS = 8u;
  EWCarStorage theEWHub[EWSLOTS];

  struct FastB {
    EWCarMetadata mEWMeta[EWSLOTS];
    H2EEP mH2El;
    T6CellO mCello;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
  };
  FAST_LOCAL(FastB,fB,b);

  typedef T6STVL<0,0,T6GRID_WIDTH,T6GRID_HEIGHT, 4, 1'000'000> T6EWLocker;
  T6EWLocker theT6EWLocker;

  void initHubForEWPs(HostBlock & hb, T6CellO & cello) {
    fB.mH2El.init();
    memset_s(&fB.mEWMeta[0],'\0',sizeof(fB.mEWMeta));
    DP.printf("IHFE10\n");
    fB.mH2El.initCars(&theEWHub[0].mEWCars[0],
                      &fB.mEWMeta[0].mEWData[0],
                      EWCarStorage::CAR_COUNT,
                      0u,
                      true);
    DP.printf("IHFE11\n");
    fB.mH2El.to_repr(DP);
  }

  void checkEWCars(HostBlock & hb) {
    static u32 spin = 0;
    for (u32 idx = 0u; idx < EWSLOTS; ++idx) {
      EWCarStorage & ews = theEWHub[idx];
      EWCarMetadata & emet = fB.mEWMeta[idx];

      if ((spin++ % 100'000'001u) == 0u)
        DP.printf("CHEWC %u idx=%u ews=0x%p meta=0x%p\n",
                  spin, idx, &ews, &emet);

      for (u32 carnum = 0u; carnum < EWCarStorage::CAR_COUNT; ++carnum) {
        EWCarStorage::EWCar & ewc = ews.mEWCars[carnum];
        if (ewc.isComplete()) {
          DP.printf("CARGP cn=%u sl=%u\n",carnum, idx);
        }
      }
    }
  }

  void updateHubCUSTOM(HostBlock & hb) {
    T6Grid & g = theT6Grid[0];
    if ((++g.mTotalChanges % 10'000'000u) == 0u)
      DP.printf("HUPD %u\n",g.mTotalChanges);
    checkEWCars(hb);
  }

  void updateHubH2E(HostBlock & hb) {
    T6Grid & g = theT6Grid[0];
    if ((++g.mTotalChanges % 10'000'000u) == 0u)
      DP.printf("HUPD %u\n",g.mTotalChanges);
    fB.mH2El.update();
  }

  void updateHub(HostBlock & hb) { updateHubH2E(hb); }

  int liveB(HostBlock & hb) {
    preloadT2Mailbox();

    if (!fB.mCello.init())
      FAIL(ILLEGAL_STATE);
    
    DP.printf("HLIB10");
    for (u8 y = 0; y < 3; ++y)
      for (u8 x = 0; x < 3; ++x) {
        U8C cp(x,y);
        u8 ic = fB.mCello.getImageCodeAtCellP(cp);
        DP.printf(" %s:%u,%u",getNameFromImageCode((ImageCode) ic),x,y);
      }
    DP.printf(".\n");

    initHubForEWPs(hb,fB.mCello);

    {
      P4Atom a = P4Atom::makeAtom(P4Atom::START_TYPE);
      U8C startc(T6GRID_WIDTH/2u,T6GRID_HEIGHT/2u);
      theT6Grid[0].setAtom(startc,a);
      DP.printf("(%d,%d) HUBSZ6G(%ux%u)->%u, %04x:%04x-%08x-%08x\n",
                hb.mPos.x,hb.mPos.y,
                T6GRID_WIDTH, T6GRID_HEIGHT, sizeof(theT6Grid),
                a.mParityAndType,a.mData0,
                a.mStg[0],a.mStg[1]
                );
    }
    
    DP.printf("(%d,%d) EWHUBSZ(%u) of %u\n",
              hb.mPos.x, hb.mPos.y,
              sizeof(theEWHub), sizeof(EWCarStorage));

    u32 spin = 0;
    while (true) {
      if ((++spin & 0x1ffff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      updateHub(hb);
    }
    return 0;
  }

  

}
