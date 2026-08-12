#include "T6Grid.h"
#include "InterHub.h"
#include "HartTasks.h" // for HTFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  T6EPL1Data<InterHubStorage,4> theInterHubL1Data;

  // InterHubStorage theInterHubStorage[4];
  // AtomicLock theInterHubLock[4];
  // InterHubEP::CarIdxs theInterHubIdxs[4];

  FAST_LOCAL_ARRAY(InterHubEP,4,myInterHubEP,n);

  static RCFlag manageInterHubNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      
      //// ONE-TIME INITS
      theInterHubL1Data.reset(); // zero all

      T6CellO cello;
      if (!cello.init()) FAIL(NO_MATCH);
      U8C ourct6 = cello.getUsCT6();
      U8C ournoc0 = U8C::makeNoC0CoordFromCT6Coord(ourct6);
      u32 ourtlbi = U8C::makeTLBIFromNoCCoord(ournoc0);
      
      // Enumerate the HUBs surrounding my (Platonic) cell
      const CellBlock & cb = cello.getOurCB();
      u32 ncount = 0u;
      S8C ourCellNum = cello.mCellNum; // cast to signed
      S8C ourstride = cb.mCellStride;
      //HBPVAL(ourCellNum);
      for (u8 d = D4_N; d <= D4_E; ++d) {
        S8C offc = S8C::makeS8CFromDir4((Dir4) d);
        //        HBPVAL(d);
        //HBPVAL(offc);
        S8C norgct6 = (ourCellNum + offc) * ourstride + cb.mLayoutOffset;
        S8C nhubct6 = norgct6 + cello.mUsCellPos;
        //HBPVAL(nhubct6);
        U8C atct6;
        if (nhubct6.toU8C(atct6)) {
          // cvt to noc0,
          // search ibas for our iba
          U8C atnoc0 = U8C::makeNoC0CoordFromCT6Coord(atct6);
          {
            S8C ngbct6off = nhubct6-ourct6;
            //            HBPTAG(BDII,ngbct6off);

            T6Neighbor ngb;
            if (ngb.init(ournoc0,ngbct6off)) {
              ImageBlockAddr usiba = ngb.findIBAIfAny(BC_INTERHUB, /*debug=*/false);
              //HBNOTE("BNGI");
              //HBXVAL((u32) usiba.mIBAMagic);
              MFM_API_ASSERT(usiba.isValid(),ILLEGAL_STATE);
              u32 theirtlbi = U8C::makeTLBIFromNoCCoord(atnoc0);
              bool weAreIn = ourtlbi < theirtlbi;

              InterHubEP & ihep = myInterHubEP[d];
              //HBPTAG(IHEP-IHUO*,&ihep);
              //HBPTAG(IHEP-WEIN,weAreIn);
              ihep.initInterHubEP({ BC_INTERHUB, (u8) d }, weAreIn, theInterHubL1Data);
              InterHubStorage & ihstg = theInterHubL1Data.mTheTCStorages[d];
              //              HBPTAG(ihepind,&ihstg);
              for (u32 c = 0; c < ihstg.getCarCount(); ++c) {
                InterHubBlock & ihb = ihstg.getTC(c);
                ihb.init();
              }

              ihep.configureDest(ournoc0, atnoc0, { BC_INTERHUB, (u8) oppositeDir4((Dir4) d) });
            } else HBPTAG(NO NGB?,ngbct6off);
          }
        }
      }

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      LOGPTAG(IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);

      HBNOTE(GOFIH);
      for (u8 d = D4_N; d <= D4_E; ++d) {
        //        HBPTAG(ihdir,d);
        //        HBPTAG(ihst,getNameFromEPState(theInterHubL1Data.getPublicEPState(d)));
        if (theInterHubL1Data.getPublicEPState(d) != EPState::CONFIGURED) continue;
        myInterHubEP[d].activate(); // release the hounds
        HBPTAG(ihsta,getNameFromEPState(theInterHubL1Data.getPublicEPState(d)));
      }
      HBMARK;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {
      SNAP(2,LOGPTAG(#IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS));
      //      HBXTAG(IHLIV,0);
      //// LIVING
      for (u32 n = 0; n < 4; ++n) {
        InterHubEP & myIHEPNC = myInterHubEP[n];
        EACH(10'000'000,{
            Dir4 d4 = (Dir4) ((__EACHNUM__/10'000'000)%4);
            Dir8 d8 = dir4ToDir8(d4);
            LOGPTAG(IHUBAROHAROHARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);
            LOGPTAG(d8,dir8ToByteString(d8));
            LOGPX(T6Grid::FULL_HEIGHT);
            LOGPX(T6Grid::FULL_WIDTH);
            LOGPX(T6Grid::SELF_ORIGIN);
            LOGPX(T6Grid::SELF_MAX);
            LOGPX(T6Grid::getCacheRange(d8));
            LOGPX(T6Grid::getCacheRange(d8).dims());
            LOGPX(T6Grid::getCacheRange(d8).area());
            LOGPTAG(cache/IHPax,(T6Grid::getCacheRange(d8).area()+InterHubPayload::MAX_ATOMS-1)/InterHubPayload::MAX_ATOMS);
          });
        //        HBPTAG(IHUO*,&myIHEPNC);
        if (!myIHEPNC.isInitted()) continue;

        //        HBPTAG(IHUO,n);
        myIHEPNC.updateOps();
        //          HBMARK;
      }
    } else LOGPTAG(unknown htoc,htoc);

    return ret;
  }
  
  __attribute__((section(".rodata_fp_table_nc")))
  HTFuncPtr interhubEPPtr = &manageInterHubNC;
}
