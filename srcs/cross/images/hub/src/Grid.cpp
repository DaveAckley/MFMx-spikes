#include "EP_Ewp.h"
#include "PT_Ewp.h"
#include "Grid.h"
#include "T6Grid.h"
#include "EwpBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;

  void GridManager::init(T6Grid & grid, ACacheBlockL1Control & acbl1, DLGridList & gridlist) {
    ONE_PING_ONLY();
    memset_s(this,'\0',sizeof(*this));
    mDLGridListPtr = &gridlist;
    mT6GridPtr = &grid;
    mACBL1Ctrl = &acbl1;
    U16C gsize = T6Grid::getGridSize();
    HBPTAG(T6GridSize,gsize);
    HBPTAG(T6GridSites,gsize.x*gsize.y);

    mAutoseedWaitCount = U32_MAX;
  }

  void DLGridList::init() {
    memset_s(this,'\0',sizeof(*this)); // zero all (lock, len,..) to start
    memset_s(mSites,'\377',sizeof(mSites)); // fill sites with cNONE
    mRoot = DL2D::cNONE;                    // and root is null
    HBPTAG(DL2Dinit,mLength);
    LOGPTAG(DL2Dinit,mLength);
  }

  bool GridManager::seekRandomNonEmptySite(U16C & found) {
    if (mAutoseedWaitCount == U32_MAX)
      mAutoseedWaitCount = create(3'000'000)+10'000'000;
    
    if (mAutoseedWaitCount > 0) {
      if (--mAutoseedWaitCount == 0) {
        MFM_API_ASSERT_NONNULL(mT6GridPtr);
        U16C ctr(DG::T6GRID_WIDTH/2,DG::T6GRID_HEIGHT/2);
        u16 type;
        switch (create(10)) {
        case 0:  type = 5; break;
        case 1:
        case 2:
        case 3:  type = 4; break;
        default: type = 2; break;
        }
        mT6GridPtr->setAtom(ctr,P4Atom::makeAtom(type)); // DREG IS TWO
        //        grid.setAtom(ctr,P4Atom::makeAtom(5)); // FB4 IS FIVE
        //        grid.setAtom(ctr,P4Atom::makeAtom(4)); // FB1 IS FOUR
        HBNOTE("AUTOSEEDOMATIC");
        LOGNOTE("LOGOAUTOSEEDOMATIC");
      } else {
        if ((mAutoseedWaitCount % 25) == 0)
          LOGPTAG(WAITING,mAutoseedWaitCount);
        return false;
      }
    }

    constexpr u32 MAX_TRIES = 5'000u;
    for (u32 i = 0u; i < MAX_TRIES; ++i) {
      U16C s = selectRandomSite();
      P4Atom a = mT6GridPtr->getAtom(s);
      if (!a.isEmpty()) {
        found = s;
        SNAP(10,HBPTAG(SEEKT,s));
        return true;
      }
    }
    return false;
  }

  bool GridManager::matchesEW(const EventWindow & ew,U16C center) const {
    T6Grid & g = *mT6GridPtr;
    S16C scenter(center);
    //    HBPTAG(scMEW@,center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      const P4Atom ga = g.getAtomOrInaccessible(gridc);
      const P4Atom ea = ew.getAtom(sn);
      if (ga != ea) {
        //LOGPTAG(NoMEW@,sn);
        //        LOGPTAG(gat,ga.getType());
        //        LOGPTAG(eat,ea.getType());
        return false;
      }
    }
    return true;
  }

  void GridManager::readEW(EventWindow & ew,U16C center) {
    T6Grid & g = *mT6GridPtr;
    //    LOGPTAG(rEWc,center);
    S16C scenter(center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      ew.getAtom(sn) = g.getAtomOrInaccessible(gridc);
    }
    //    LOGATOM(ew.getAtom(0));
  }

  u32 GridManager::writeEW(const EventWindow & ew,U16C center) {
    T6Grid & g = *mT6GridPtr;
    u32 changes = g.getTotalChanges();
    S16C scenter(center);
    EACH(100'000,LOGPTAG(gmwEWc,center));
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      const P4Atom atom = ew.getAtom(sn);
      if (g.setAtomOrDrop(gridc,atom)) {
        // atom valid and was different than before
        // so add it to the DLGridList
        //        LOGPTAG(gmwEWdc,gridc);
        //        LOGATOM(atom);
        EACH(100'000,LOGATOM(g.getAtom(gridc)));
        bool b = mDLGridListPtr->pushFrontC(U8C(gridc.x,gridc.y));
        if (b) EACH(100'000,LOGPTAG(oldATOM,gridc));
        else EACH(100'000,LOGPTAG(newATOM,gridc));
        EACH(100'000,LOGPTAG(ATOMS,mDLGridListPtr->getLength()));

      }
    }
    return g.getTotalChanges()-changes;
  }

  void GridManager::applyEWT(EwpPayload &ewt) {
    /* (1) check if ewt applicable
           (1.1) if so, apply it
       (2) select new event if possible
           (2.1) load ewt. (have some kind 
                 of placeholder if not.)
     */
    //    SNAP(100,HBNOTE(applyEWT));
    U16C center;
    // if has dest, try to apply
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_AND_DEST) {
      center.x = ewt.mHiddenXPos;
      center.y = ewt.mHiddenYPos;
      SNAP(100,HBPTAG(gmtraply,center));
      if (matchesEW(ewt.mOld,center)) {
        SNAP(100,HBPTAG(gmaplid!,center));
        EACH(100'000,LOGPTAG(!MEW@,center));
        writeEW(ewt.mNew,center);
      }
    }
    // all EWPCs come through here
    EACH(100,HBNOTE(applyEWT-Seek));
    if (seekRandomNonEmptySite(center)) {
      SNAP(10,HBNOTE(readEW));
      readEW(ewt.mOld,center);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_ONLY;
      ewt.mHiddenXPos = center.x;
      ewt.mHiddenYPos = center.y;
      EACH(100'000,LOGPTAG(gmdisnew,center));
    } else {                    // couldn't find a center
      ++mEWsEmptiesShipped;
      SNAP(10,HBPTAG(noCtr,mEWsEmptiesShipped));
      if ((mEWsEmptiesShipped%100000)==0) LOGPTAG(gmdedhed,mEWsEmptiesShipped);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_EMPTY;
    }
  }
}
