#include "InterHub.h"
#include "FastNC.h" // for EPFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  T6EPL1Data<InterHubStorage,4> theInterHubL1Data;

  // InterHubStorage theInterHubStorage[4];
  // AtomicLock theInterHubLock[4];
  // InterHubEP::CarIdxs theInterHubIdxs[4];

  FAST_LOCAL_ARRAY(InterHubEP,4,myInterHubEP,n);

  static bool manageInterHubNC(bool doInit) {
    bool ret = false;

    if (unlikely(doInit)) {
      HBNOTE("IHUBARO");
      //// ONE-TIME INITS
      theInterHubL1Data.reset(); // zero all

      T6CellO cello;
      if (!cello.init()) FAIL(NO_MATCH);
      U8C ourct6 = cello.getUsCT6();
      U8C ournoc0 = U8C::makeNoC0CoordFromCT6Coord(ourct6);
      u32 ourtlbi = U8C::makeTLBIFromNoC0Coord(ournoc0);
      
      // Enumerate the HUBs surrounding my (Platonic) cell
      const CellBlock & cb = cello.getOurCB();
      u32 ncount = 0u;
      S8C ourCellNum = cello.mCellNum; // cast to signed
      S8C ourstride = cb.mCellStride;
      HBPVAL(ourCellNum);
      for (u8 d = D4_N; d <= D4_E; ++d) {
        HBPVAL(d);
        S8C offc = S8C::makeS8CFromDir4((Dir4) d);
        HBPVAL(offc);
        S8C norgct6 = (ourCellNum + offc) * ourstride + cb.mLayoutOffset;
        S8C nhubct6 = norgct6 + cello.mUsCellPos;
        HBPVAL(nhubct6);
        U8C atct6;
        if (nhubct6.toU8C(atct6)) {
          // cvt to noc0,
          // search ibas for our iba
          U8C atnoc0 = U8C::makeNoC0CoordFromCT6Coord(atct6);
          HBNOTE("BONDII");
          HBPVAL(atnoc0);
          {
            S8C ngbct6off = nhubct6-ourct6;
            HBPVAL(ngbct6off);
            T6Neighbor ngb;
            if (ngb.init(ournoc0,ngbct6off)) {
              ImageBlockAddr usiba = ngb.findIBAIfAny(BC_INTERHUB);
              HBNOTE("BNGI");
              HBXVAL((u32) usiba.mIBAMagic);
              MFM_API_ASSERT(usiba.isValid(),ILLEGAL_STATE);
              u32 theirtlbi = U8C::makeTLBIFromNoC0Coord(atnoc0);
              bool weAreIn = ourtlbi < theirtlbi;

              InterHubEP & ihep = myInterHubEP[d];
              ihep.initInterHubEP(BC_INTERHUB, weAreIn, theInterHubL1Data);

              InterHubStorage & ihstg = theInterHubL1Data.mTheTCBlocks[d];
              for (u32 c = 0; c < ihstg.getCarCount(); ++c) {
                InterHubBlock & ihb = ihstg.getTC(c);
                ihb.init();
              }

              ihep.configureDest(ournoc0, atnoc0, oppositeDir4((Dir4) d));
            }
          }
        }
      }
      HBMARK;
    } else {
      SNAP(8,HBNOTE("IHLIV"));
      //// LIVING
      for (u32 n = 0; n < 4; ++n) {
        InterHubEP & myIHEPNC = myInterHubEP[n];
        if (!myIHEPNC.isInitted()) continue;

        SNAP(8,HBPVAL(n));
        if (myIHEPNC.updateOps()) {
          ret = true;
          HBMARK;
        }
      }
    }

    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  EPFuncPtr interhubEPPtr = &manageInterHubNC;
}
