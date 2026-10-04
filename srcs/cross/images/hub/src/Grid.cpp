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

    // INIT AUTOSEEDING
    mAutoseedWaitCount = U32_MAX;

    // INIT SUPERCELL POSITIONING
    HostBlock & hb = theHostBlock;
    U8C ct6 = U8C::makeCT6CoordFromTLBI(hb.mTLBI);
    U8C tt(2,2);
    
    U8C hubInGridCoord = ct6 / tt;
    U8C supercellc = hubInGridCoord % tt;
    mSuperCellLeaderCode = U8C::makeLeaderCodeFromSuperCellCoord(supercellc);
    
  }

  void DLGridList::init() {
    memset_s(this,'\0',sizeof(*this)); // zero all (lock, len,..) to start
    memset_s(mSites,'\377',sizeof(mSites)); // fill sites with cNONE
    mRoot = DL2D::cNONE;                    // and root is null
    HBPTAG(DL2Dinit,mLength);
    LOGPTAG(DL2Dinit,mLength);
  }

  SCStatus GridManager::leadFollowOrGetOutOfWay() const {
    L1GridManagerControl & lgmc = theL1GridManagerControl;
    if (lgmc.mSuperCellLeader > 3) return SCStatus::NO_LEADER;
    if (lgmc.mSuperCellLeader == mSuperCellLeaderCode) return SCStatus::WE_LEAD;
    return SCStatus::WE_FOLLOW; 
  }

  bool GridManager::seekRandomNonEmptySite(U16C & found) {
    if (mAutoseedWaitCount == U32_MAX)
      mAutoseedWaitCount = /*create(1'000'000)+*/1'000'000;
    
    if (mAutoseedWaitCount > 0) {
      if (--mAutoseedWaitCount == 0) {
        MFM_API_ASSERT_NONNULL(mT6GridPtr);
        U16C ctr(DG::T6GRID_WIDTH/2,DG::T6GRID_HEIGHT/2);
        u16 type;
        switch (create(50)) {
          // LET'S TRY NO FB4: case 0:  type = 5; break;
          //        case 1:
          // ALMOST ALL DREG!
          //case 2:
          //case 3:
        case 4:  type = 4; break;
        default: type = 2; break;
        }
        mT6GridPtr->setAtom(ctr,P4Atom::makeAtom(type)); // DREG IS TWO
        //        grid.setAtom(ctr,P4Atom::makeAtom(5)); // FB4 IS FIVE
        //        grid.setAtom(ctr,P4Atom::makeAtom(4)); // FB1 IS FOUR
        HBPTAG("AUTOSEEDOMATIC",type);
        LOGPTAG("LOGOAUTOSEEDOMATIC",type);
      } else {
        if ((mAutoseedWaitCount % 1'000'000) == 0)
          LOGPTAG(WAITING,mAutoseedWaitCount);
        return false;
      }
    }

    /// CHECK CURRENT EVENT BOUNDS
    L1GridManagerControl & lgmc = theL1GridManagerControl;
    EACH(100'000,{
        LOGPTAG(SCLC-cur,(u32) lgmc.mSuperCellLeader);
        LOGPTAG(SCLC-us,(u32) mSuperCellLeaderCode);
      });

    U16CRange ewbounds;
    SCStatus s = leadFollowOrGetOutOfWay();
    if (s == SCStatus::NO_LEADER) {
      // there is currently no SCL: avoid the caches
      ewbounds.start.x = T6Grid::SELF_ORIGIN.x+4; //inclusive
      ewbounds.stop.x = T6Grid::SELF_MAX.x-4;     //exclusive
      ewbounds.start.y = T6Grid::SELF_ORIGIN.y+4; //inclusive
      ewbounds.stop.y = T6Grid::SELF_MAX.y-4;     //exclusive

    } else if (s == SCStatus::WE_LEAD) {
      // we are currently the SCL: own the caches
      ewbounds.start.x = 0+4;                     //inclusive
      ewbounds.stop.x = T6Grid::FULL_WIDTH-4;     //exclusive
      ewbounds.start.y = 0+4;                     //inclusive
      ewbounds.stop.y = T6Grid::FULL_HEIGHT-4;    //exclusive

    } else { /* s == some kind of follower */
      // we are currently an SCL follower: avoid our own edges
      ewbounds.start.x = T6Grid::SELF_ORIGIN.x+DG::T6GRID_OVERLAP_WIDTH+4;  //inclusive
      ewbounds.stop.x = T6Grid::SELF_MAX.x-DG::T6GRID_OVERLAP_WIDTH-4;      //exclusive
      ewbounds.start.y = T6Grid::SELF_ORIGIN.y+DG::T6GRID_OVERLAP_HEIGHT+4; //inclusive
      ewbounds.stop.y = T6Grid::SELF_MAX.y-DG::T6GRID_OVERLAP_HEIGHT-4;     //exclusive
    }
    
    constexpr u32 MAX_TRIES = 5'000u;
    for (u32 i = 0u; i < MAX_TRIES; ++i) {
      U16C s = selectRandomSite(ewbounds);
      P4Atom a = mT6GridPtr->getAtom(s);
      if (!a.isEmpty()) {
        found = s;
        SNAP(10,HBPTAG(SEEKT,s));
        return true;
      }
    }
    return false;
  }

  U16C GridManager::selectRandomSite(U16CRange b) {
    //    return U16C((u16) between(T6Grid::SELF_ORIGIN.x+4,T6Grid::SELF_MAX.x-4-1),
    //                (u16) between(T6Grid::SELF_ORIGIN.y+4,T6Grid::SELF_MAX.y-4-1));
    return U16C((u16) between(b.start.x,b.stop.x-1),
                (u16) between(b.start.y,b.stop.y-1));
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
        EACH(1'000'000,LOGATOM(g.getAtom(gridc)));
        bool b = mDLGridListPtr->pushFrontC(U8C(gridc.x,gridc.y));
        if (b) EACH(1'000'000,LOGPTAG(oldATOM,gridc));
        else EACH(1'000'000,LOGPTAG(newATOM,gridc));
        EACH(1'000'000,LOGPTAG(ATOMS,mDLGridListPtr->getLength()));

      }
    }
    return g.getTotalChanges()-changes;
  }

  void GridManager::applyEWT(EwpPayload &ewt, u32 ngbidx) {
    /* (1) check if ewt applicable
           (1.1) if so, apply it
       (2) select new event if possible
           (2.1) load ewt. (have some kind 
                 of placeholder if not.)
     */

    /* but if EWs suspended, 
       (1) Don't apply any returned EWTs, and
       (2) Don't send any new non-empty EWTs
    */
    U16C center;
    L1GridManagerControl & lgmc = theL1GridManagerControl;
    if (!lgmc.isEPSuspReq() &&
        ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_AND_DEST) {

      // not suspended and it has a dest: try to apply
      center.x = ewt.mHiddenXPos;
      center.y = ewt.mHiddenYPos;
      SNAP(100,HBPTAG(ewtraply,center));
      if (matchesEW(ewt.mOld,center)) {
        SNAP(100,HBPTAG(ewaplid!,center));
        EACH(100'000,LOGPTAG(!MEW@,center));
        writeEW(ewt.mNew,center);
      }
    }

    // all EWPCs come through here
    EACH(100'000,HBPTAG(applyEWT-Seek,__EACHNUM__));
    if (!lgmc.isEPSuspReq() && seekRandomNonEmptySite(center)) {
      SNAP(10,HBNOTE(readEW));
      readEW(ewt.mOld,center);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_ONLY;
      ewt.mHiddenXPos = center.x;
      ewt.mHiddenYPos = center.y;
      EACH(1'000'000,LOGPTAG(ewdisnew,center));
    } else {                    // suspended or couldn't find a center
      ++mEWsEmptiesShipped;
      SNAP(10,HBPTAG(noCtr,mEWsEmptiesShipped));
      if ((mEWsEmptiesShipped%1'000'000)==0) LOGPTAG(ewdedhed,mEWsEmptiesShipped);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_EMPTY;
    }

    if (lgmc.isEPSuspReq() != lgmc.isEPSuspReqSeen())
      lgmc.setEPSuspReqSeen(lgmc.isEPSuspReq());
  }
}
