#pragma once  /* -*- C++ -*- */
#include "T6Grid.h"
#include "SharedTCs.h"
#include "SharedTCs.h"
#include "S16C.h"
#include "UxC.h" // For U16C
#include "FastT2.h" // for between(..)
#include "MDist.h"
#include "DemoGlobal.h"

namespace MFM {
  struct GridManager {
    void init(T6Grid & grid) {
      memset_s(this,'\0',sizeof(*this));
      mT6GridPtr = &grid;
      U16C gsize = T6Grid::getGridSize();
      HBPTAG(T6GridSize,gsize);
      HBPTAG(T6GridSites,gsize.x*gsize.y);
    }

    U16C selectRandomSite() {
      return U16C((u16) between(0u,DG::T6GRID_WIDTH-1),
                  (u16) between(0u,DG::T6GRID_HEIGHT-1));
    }

    S16C siteNumberToOffset(u32 sn) const {
      MDist4 md;
      SPoint sp = md.GetPoint(sn); // or fail
      return S16C(sp);
    }

    bool seekRandomNonEmptySite(U16C & found) ;

    bool matchesEW(const EventWindow & ew,U16C center) const ;

    void readEW(EventWindow & ew,U16C center) ;

    u32 writeEW(const EventWindow & ew,U16C center) ;

    void applyEWT(EwpPayload &ewt) ;

    T6Grid * mT6GridPtr;
    u32 mEWsOffered;
    u32 mEWsEmptiesShipped;
    u32 mEWsReturned;
    u32 mEWsCommitted;
    u32 mEWsObsoleted;
    u32 mTotalAtomicChanges;
  };
}
