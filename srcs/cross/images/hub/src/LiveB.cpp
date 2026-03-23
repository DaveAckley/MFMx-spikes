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

//#define P DP
#define P LOG

namespace MFM {

  extern int liveB(HostBlock & hb) __attribute__ ((optimize("O2")));

  T6Grid theT6Grid[1] __attribute__ ((section(".crossrodata")));

  static constexpr u32 EWSLOTS = 8u;
  EWCarStorage theEWHub[EWSLOTS];

  struct FastB {
    EWCarMetadata mEWMeta[EWSLOTS];
    H2EEP mH2EEPs[EWSLOTS];
    T6CellO mCellO;
    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
  };
  FAST_LOCAL(FastB,fB,b);

  typedef T6STVL<0,0,T6GRID_WIDTH,T6GRID_HEIGHT, 4, 1'000'000> T6EWLocker;
  T6EWLocker theT6EWLocker;

  void initHubForEWPs(u32 ewpidx, HostBlock & hb, T6CellO & cello, U8C ewpnoc0, u32 remotebaseaddr) {
    H2EEP & h2e = fB.mH2EEPs[ewpidx];
    h2e.init();
    memset_s(&fB.mEWMeta[ewpidx],'\0',sizeof(fB.mEWMeta[ewpidx]));
    //P.printf("IHFE10 %u\n",ewpidx);
    h2e.initCars(hb.mPos,       // our noc0
                 ewpidx, ewpnoc0,
                 &theEWHub[ewpidx].mEWCars[0],
                 &fB.mEWMeta[ewpidx].mEWData[0],
                 EWCarStorage::CAR_COUNT,
                 remotebaseaddr,
                 true);
    //P.printf("IHFE11 %u\n",ewpidx);
    //h2e.to_repr(DP);
  }

  struct EWPNgbs {
    static constexpr u32 MAX_EWP_NGBS = 7u*2u;
    u32 mEWBaseL1Addrs[MAX_EWP_NGBS];
    U8C mNoC0s[MAX_EWP_NGBS];

    U8C getNoC0OfEWP(u32 ewpidx) {
      MFM_API_ASSERT(ewpidx<MAX_EWP_NGBS,ILLEGAL_ARGUMENT);
      return mNoC0s[ewpidx];
    }
    u32 getBaseL1AddressOfEWP(u32 ewpidx) {
      MFM_API_ASSERT(ewpidx<MAX_EWP_NGBS,ILLEGAL_ARGUMENT);
      return mEWBaseL1Addrs[ewpidx];
    }
    void reset() {
      memset_s(this,'\0',sizeof(*this));
    }
    void init(HostBlock & hb, T6CellO & cello) {
      reset();
      CellBlock & ourcb = *(CellBlock*) ((void*) cello.mOurCellBlockAddr);
      U8C cs = ourcb.mCellSize;
      U8C usnoc0 = hb.mPos;
      U8C usCT6 = cello.getUsCT6();
      U8C cellCT6 = cello.getCellOriginCT6(); 
      u32 index = 0u;
      for (u8 y = 0; y < cs.y; ++y) {
        for (u8 x = 0; x < cs.x; ++x) {
          U8C cp(x,y);
          if (cp == usCT6) continue;
          u32 ewpidx = index++; // index incrs for cell size excluding us, not accessible size
          U8C ngbCT6 = cellCT6 + cp;
          S8C offct6 = S8C(ngbCT6) - usCT6;
          T6NgbL1Block nbl;
          if (!nbl.init(usnoc0, offct6, BlockCode::BC_EWCARS) ||
              !nbl.isValid())
            continue;
          mEWBaseL1Addrs[ewpidx] = nbl.mBaseAddress;
          mNoC0s[ewpidx] = nbl.mT6Ngb.mNoC0Ngb;
          if (false) P.printf("HENI %d %u,%u 0x%x\n",
                    ewpidx,
                    mNoC0s[ewpidx].x, mNoC0s[ewpidx].y,
                    mEWBaseL1Addrs[ewpidx]);
        }
      }
    }
  };

  void checkEWCars(HostBlock & hb) {
    static u32 spin = 0;
    for (u32 idx = 0u; idx < EWSLOTS; ++idx) {
      EWCarStorage & ews = theEWHub[idx];
      EWCarMetadata & emet = fB.mEWMeta[idx];

      if ((spin++ % 100'000'001u) == 0u)
        P.printf("CHEWC %u idx=%u ews=0x%p meta=0x%p\n",
                  spin, idx, &ews, &emet);

      for (u32 carnum = 0u; carnum < EWCarStorage::CAR_COUNT; ++carnum) {
        EWCarStorage::EWCar & ewc = ews.mEWCars[carnum];
        if (ewc.isComplete()) {
          P.printf("CARGP cn=%u sl=%u\n",carnum, idx);
        }
      }
    }
  }

  void updateHubCUSTOM(HostBlock & hb) {
    T6Grid & g = theT6Grid[0];
    if ((++g.mTotalChanges % 10'000'000u) == 0u)
      P.printf("HUPD %u\n",g.mTotalChanges);
    checkEWCars(hb);
  }

  void updateHubH2E(u8 ewpidx, HostBlock & hb) {
    T6Grid & g = theT6Grid[0];
    if ((++g.mTotalChanges % 10'000'000u) == 0u)
      P.printf("HUPD %u\n",g.mTotalChanges);
    fB.mH2EEPs[ewpidx].update();
  }

  void updateHub(HostBlock & hb) {
    for (u8 ewpidx = 0u; ewpidx < EWSLOTS; ++ewpidx) {
      updateHubH2E(ewpidx,hb);
    }
  }

  int liveB(HostBlock & hb) {
    preloadT2Mailbox();

    if (!fB.mCellO.init())
      FAIL(ILLEGAL_STATE);
    
    EWPNgbs theMinions;
    theMinions.init(hb,fB.mCellO);
    
    //P.printf("HLIB10 N:%u E:%u B:%u W:%u\n",sizeof(theMinions),8*sizeof(H2EEP),sizeof(fB),sizeof(EventWindow));

    for (u32 i = 0u; i < EWSLOTS; ++i) {
      initHubForEWPs(i,hb,fB.mCellO,theMinions.getNoC0OfEWP(i),theMinions.getBaseL1AddressOfEWP(i));
      if (false) P.printf("XLB0  %u/%p\n",i,fB.mH2EEPs[i].getBaseCarMetadata());
    }

    if (false) for (u32 i = 0u; i < EWSLOTS; ++i)
      P.printf("XLB10 %u/%p\n",i,fB.mH2EEPs[i].getBaseCarMetadata());

    {
      P4Atom a = P4Atom::makeAtom(P4Atom::START_TYPE);
      U8C startc(T6GRID_WIDTH/2u,T6GRID_HEIGHT/2u);
      theT6Grid[0].setAtom(startc,a);
      if (false)
        P.printf("(%d,%d) HUBSZ6G(%ux%u)->%u, %04x:%04x-%08x-%08x\n",
                hb.mPos.x,hb.mPos.y,
                T6GRID_WIDTH, T6GRID_HEIGHT, sizeof(theT6Grid),
                a.mParityAndType,a.mData0,
                a.mStg[0],a.mStg[1]
                );
    }
    
    if (false)
      P.printf("(%d,%d) EWHUBSZ(%u) of %u\n",
                hb.mPos.x, hb.mPos.y,
                sizeof(theEWHub), sizeof(EWCarStorage));

    u32 spin = 0;
    if (false) for (u32 i = 0; i < EWSLOTS; ++i)
      P.printf("XLB11 %u/%p\n",i,fB.mH2EEPs[i].getBaseCarMetadata());
    while (true) {
      if ((++spin & 0x1ffff) == 0) {
        hb.hartbeat(fAll.mHartNum);
      }
      updateHub(hb);
    }
    return 0;
  }

  

}
