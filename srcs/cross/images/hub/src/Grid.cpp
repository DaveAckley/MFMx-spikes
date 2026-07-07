#include "EP_Ewp.h"
#include "PT_Ewp.h"
#include "Grid.h"
#include "T6Grid.h"
#include "EwpBlock.h"

namespace MFM {

  bool GridManager::seekRandomNonEmptySite(U16C & found) {
    constexpr u32 MAX_TRIES = 100u;
    for (u32 i = 0u; i < MAX_TRIES; ++i) {
      U16C s = selectRandomSite();
      P4Atom a = mT6GridPtr->getAtom(s);
      if (!a.isEmpty()) {
        found = s;
        //        HBPTAG(FOUNDER!,s);
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
        HBPTAG(NoMEW@,sn);
        HBPTAG(gat,ga.getType());
        HBPTAG(eat,ea.getType());
        return false;
      }
    }
    return true;
  }

  void GridManager::readEW(EventWindow & ew,U16C center) {
    T6Grid & g = *mT6GridPtr;
    S16C scenter(center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      ew.getAtom(sn) = g.getAtomOrInaccessible(gridc);
    }
  }

  u32 GridManager::writeEW(const EventWindow & ew,U16C center) {
    T6Grid & g = *mT6GridPtr;
    u32 changes = g.getTotalChanges();
    S16C scenter(center);
    //    LOGPTAG(gmwEWc,center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      const P4Atom atom = ew.getAtom(sn);
      if (g.setAtomOrDrop(gridc,atom)) {
        // atom valid and was different than before
        // so add it to the DLGridList
        bool b = mDLGridListPtr->pushFrontC(U8C(gridc.x,gridc.y));
        if (b) LOGPTAG(oldATOM,gridc);
        else LOGPTAG(newATOM,gridc);
        LOGPTAG(ATOMS,mDLGridListPtr->getLength());

        /*
        bool b = mACBL1Ctrl->writeAtom(atom, gridc);
        if (b) LOGPTAG(wrATOM,gridc);
        else LOGPTAG(drATOM,sn);
        */
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
    //    HBNOTE(applyEWT);
    U16C center;
    // if has dest, try to apply
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_AND_DEST) {
      center.x = ewt.mHiddenXPos;
      center.y = ewt.mHiddenYPos;
      //      HBPTAG(gmtraply,center);
      if (matchesEW(ewt.mOld,center)) {
        HBPTAG(gmaplid!,center);
        writeEW(ewt.mNew,center);
      }
    }
    // all EWPCs come through here
    if (seekRandomNonEmptySite(center)) {
      readEW(ewt.mOld,center);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_ONLY;
      ewt.mHiddenXPos = center.x;
      ewt.mHiddenYPos = center.y;
      HBPTAG(gmdisnew,center);
    } else {                    // couldn't find a center
      ++mEWsEmptiesShipped;
      //      HBPTAG(gmdedhed,mEWsEmptiesShipped);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_EMPTY;
    }
  }
}
