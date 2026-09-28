#include "T6Grid.h"
#include "InterHub.h"
#include "HartTasks.h" // for HTFuncPtr
#include "T6CellO.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  FAST_LOCAL_ARRAY(InterHubEP,8,myInterHubEP,n);

  static RCFlag manageInterHubNC(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;

    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {
      
      //// ONE-TIME INITS
      theInterHubL1Data.reset(); // zero all
      theCornerHubL1Data.reset(); 

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
      for (u8 d = D8_NT; d <= D8_NE; ++d) {
        Dir8 d8 = (Dir8) d;
        S8C offc = S8C::makeS8CFromDir8(d8);
        S8C norgct6 = (ourCellNum + offc) * ourstride + cb.mLayoutOffset;
        S8C nhubct6 = norgct6 + cello.mUsCellPos;
        U8C atct6;
        if (nhubct6.toU8C(atct6)) {
          // cvt to noc0,
          // search ibas for our iba
          U8C atnoc0 = U8C::makeNoC0CoordFromCT6Coord(atct6);
          {
            S8C ngbct6off = nhubct6-ourct6;

            T6Neighbor ngb;
            if (ngb.init(ournoc0,ngbct6off)) {
              ImageBlockAddr usiba = ngb.findIBAIfAny(BC_INTERHUB, /*debug=*/false);
              MFM_API_ASSERT(usiba.isValid(),ILLEGAL_STATE);
              u32 theirtlbi = U8C::makeTLBIFromNoCCoord(atnoc0);
              bool weAreIn = ourtlbi < theirtlbi;

              InterHubEP & ihep = myInterHubEP[d8];
              HBPTAG(8IHEP-IHUO*,&ihep);
              HBPTAG(8IHEP-WEIN,weAreIn);
              ihep.initInterHubEP({ BC_INTERHUB, d8 }, weAreIn, theInterHubL1Data);
              InterHubStorage & ihstg = theInterHubL1Data.mTheTCStorages[d8];
              HBPTAG(8IHEP-srcD8,dir8ToByteString(d8));
              for (u32 c = 0; c < ihstg.getCarCount(); ++c) {
                InterHubBlock & ihb = ihstg.getTC(c);
                ihb.init();
                HBPTAG(8IHEP-car,c);
              }

              ihep.configureDest(ournoc0, atnoc0, { BC_INTERHUB, (u8) oppositeDir8(d8) });
              HBPTAG(8IHEP-dstD8,dir8ToByteString(oppositeDir8(d8)));
            } else HBPTAG(NO NGB?,ngbct6off);
          }
        }
      }

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      //LOGPTAG(IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);

      HBNOTE(GOFIH);
      for (u8 d8 = D8_NT; d8 <= D8_NE; ++d8) {
        if (theInterHubL1Data.getPublicEPState(d8) != EPState::CONFIGURED) continue;
        myInterHubEP[d8].activate(); // release the hounds
        HBPTAG(ihsta,getNameFromEPState(theInterHubL1Data.getPublicEPState(d8)));
      }
      HBMARK;

    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {
      //SNAP(2,LOGPTAG(#IHUBARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS));
      //      HBXTAG(IHLIV,0);
      //// LIVING
      for (u32 d = D8_NT; d <= D8_NE; ++d) {
        Dir8 d8 = (Dir8) d;
        InterHubEP & myIHEPNC = myInterHubEP[d8];
        EACH(10'000'000,{
            //LOGPTAG(IHUBAROHAROHARO:MAX_ATOMS,InterHubPayload::MAX_ATOMS);
            LOGPTAG(d8,dir8ToByteString(d8));
            LOGPX(T6Grid::FULL_HEIGHT);
            LOGPX(T6Grid::FULL_WIDTH);
            LOGPX(T6Grid::SELF_ORIGIN);
            LOGPX(T6Grid::SELF_MAX);
            LOGPX(T6Grid::getCacheRange(d8));
            LOGPX(T6Grid::getCacheRange(d8).dims());
            LOGPX(T6Grid::getCacheRange(d8).area());
            //LOGPTAG(cache/IHPax,(T6Grid::getCacheRange(d8).area()+InterHubPayload::MAX_ATOMS-1)/InterHubPayload::MAX_ATOMS);
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
