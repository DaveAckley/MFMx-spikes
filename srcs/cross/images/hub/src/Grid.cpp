#include "EP_Ewp.h"
#include "PT_Ewp.h"
#include "Grid.h"
#include "T6Grid.h"
#include "EwpBlock.h"
#include "DemoGlobal.h"
#include "CachingPolicy.h" // supercell vs four corners event bounds
#include "HubEdges.h"      // permanent array edges
#include "EthSeam.h"

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

    // INIT SUPERCELL POSITIONING
    HostBlock & hb = theHostBlock;
    u32 tlbi = hb.mTLBI;
    U8C ct6 = U8C::makeCT6CoordFromTLBI(tlbi);
    U8C tt(2,2);

    // Hubs are counted GLOBALLY across chips (see DemoGlobal.h), so
    // that hubs that touch across a chip seam are never both leader.
    U8C hubInChipCoord = ct6 / tt;
    U8C supercellc = DG::getHubSuperCellCoord(hb.mChipNum,
                                              hubInChipCoord.x,
                                              hubInChipCoord.y);
    mSuperCellLeaderCode = U8C::makeLeaderCodeFromSuperCellCoord(supercellc);
    
    // PERMANENT EDGES: a side with no neighbor hub on this chip or across a
    // live seam is a real array edge (HubEdges.h), not a dead band.
    {
      EthSeam::SeamTable seams;
      EthSeam::decodeSeamMask(hb.getEthSeamMask(), seams);
      mPermEdges = HubEdges::permanentEdges(seams, ct6);
      LOGPTAG(.pe,(u32) mPermEdges);   // dotted: survives HBF_NO_UTAG
      HBPTAG(.pe,(u32) mPermEdges);
    }

    // INIT SELECT AUTOSEEDING (the host can switch a whole chip off,
    // HBF_NO_AUTOSEED, to check that atoms really cross the seams)
    if (ct6.x >= 5 && ct6.y >= 5 &&
        ct6.x <= 6 && ct6.y <= 6 &&
        (hb.mHostFlags & HostFlags::HBF_NO_AUTOSEED) == 0)
      mAutoseedWaitCount = U32_MAX;
    else
      mAutoseedWaitCount = 0; // no soup for you

  }

  void DLGridList::init() {
    memset_s(this,'\0',sizeof(*this)); // zero all (lock, len,..) to start
    memset_s(mSites,'\377',sizeof(mSites)); // fill sites with cNONE
    mRoot = DL2D::cNONE;                    // and root is null
    HBPTAG(DL2Dinit,mLength);
    LOGPTAG(DL2Dinit,mLength);
  }

  SCStatus GridManager::leadFollowOrGetOutOfTheWay() const {
    L1GridManagerControl & lgmc = theL1GridManagerControl;
    if (lgmc.mSuperCellLeader > 3) return SCStatus::NO_LEADER;
    if (lgmc.mSuperCellLeader == mSuperCellLeaderCode) return SCStatus::WE_LEAD;
    return SCStatus::WE_FOLLOW; 
  }

  bool GridManager::seekRandomNonEmptySite(U16C & found) {
    if (mAutoseedWaitCount == U32_MAX)
      mAutoseedWaitCount = create(500'000)+500'000;
    
    if (mAutoseedWaitCount > 0) {
      if (--mAutoseedWaitCount == 0) {
        MFM_API_ASSERT_NONNULL(mT6GridPtr);
        U16C ctr(DG::T6GRID_WIDTH/2,DG::T6GRID_HEIGHT/2);
        // The seed type comes from the host (mfmxctl 'decay N',
        // CodeManager::setStartDecayType -> mCommonArgs[1]); U16_MAX
        // means unset, which keeps the old default of FB4 (five).
        // FB1 is four, DREG is two.
        u16 type = 5;
        if (theHostBlock.mCommonArgs[1] != U16_MAX)
          type = (u16) theHostBlock.mCommonArgs[1];
        setAtom(ctr,P4Atom::makeAtom(type));
        HBPTAG("AUTOSEEDOMATIC",type);
        LOGPTAG("LOGOAUTOSEEDOMATIC",type);
        LOGPTAG(.as,type);   // dotted: survives HBF_NO_UTAG (seed atom type)
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

    // The bounds come from the run's caching policy (CachingPolicy.h):
    // supercell leader/follower, or four corners.  Either way an event
    // window may only touch sites this hub owns this supercycle.
    SCStatus s = leadFollowOrGetOutOfTheWay();
    PolicyCtx pc;
    pc.noLeader = s == SCStatus::NO_LEADER;   // no argument yet: avoid the caches
    pc.weLead = s == SCStatus::WE_LEAD;       // (supercell policy only)
    pc.peerLeads = false;
    const EventBounds eb =
      CachingPolicies::withPermanentEdges(
        CachingPolicies::bounds(cachePolicyFromHostFlags(theHostBlock.mHostFlags),
                                (u8) lgmc.mSuperCellLeader,
                                pc),
        mPermEdges);
    U16CRange ewbounds;
    ewbounds.start.x = eb.x0;   //inclusive
    ewbounds.stop.x = eb.x1;    //exclusive
    ewbounds.start.y = eb.y0;   //inclusive
    ewbounds.stop.y = eb.y1;    //exclusive

    // Each miss is an event at an empty center and counts toward AER, so a
    // big limit makes the AER of a sparse grid (e.g. right after the seed)
    // huge and warps comparisons: 100 keeps that distortion modest.
    constexpr u32 MAX_TRIES = 100u;
    for (u32 i = 0u; i < MAX_TRIES; ++i) {
      U16C s = selectRandomSite(ewbounds);
      P4Atom a = mT6GridPtr->getAtom(s);
      if (!a.isEmpty()) {
        mEWsEmptyCenters += i;  // i draws hit empty sites: events that did nothing
        found = s;
        SNAP(10,HBPTAG(SEEKT,s));
        return true;
      }
    }
    mEWsEmptyCenters += MAX_TRIES; // all of them did nothing
    return false;
  }

  U16C GridManager::selectRandomSite(U16CRange b) {
    //    return U16C((u16) between(T6Grid::SELF_ORIGIN.x+4,T6Grid::SELF_MAX.x-4-1),
    //                (u16) between(T6Grid::SELF_ORIGIN.y+4,T6Grid::SELF_MAX.y-4-1));
    return U16C((u16) between(b.start.x,b.stop.x-1),
                (u16) between(b.start.y,b.stop.y-1));
  }

  bool GridManager::matchesEW(const EventWindow & ew,U16C center) const {
    S16C scenter(center);
    //    HBPTAG(scMEW@,center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      const P4Atom ga = getAtomOrInaccessible(gridc);
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
    S16C scenter(center);
    for (u32 sn = 0u; sn < EventWindow::ATOM_COUNT; ++sn) {
      S16C offc = siteNumberToOffset(sn);
      U16C gridc(scenter.x+offc.x,scenter.y+offc.y);
      ew.getAtom(sn) = getAtomOrInaccessible(gridc);
    }
  }

  bool GridManager::isAccessible(U16C gridc) const {
    if (!isValidT6GridC(gridc)) return false;
    if ((mPermEdges & HubEdges::PE_W) && gridc.x < T6Grid::SELF_ORIGIN.x) return false;
    if ((mPermEdges & HubEdges::PE_E) && gridc.x >= T6Grid::SELF_MAX.x) return false;
    if ((mPermEdges & HubEdges::PE_N) && gridc.y < T6Grid::SELF_ORIGIN.y) return false;
    if ((mPermEdges & HubEdges::PE_S) && gridc.y >= T6Grid::SELF_MAX.y) return false;
    return true;
  }

  P4Atom GridManager::getAtomOrInaccessible(U16C gridc) const {
    if (!isAccessible(gridc)) return P4Atom::makeInaccessibleAtom();
    return mT6GridPtr->getAtom(gridc);
  }

  bool GridManager::setAtom(U16C gridc, const P4Atom atom) {
    MFM_API_ASSERT(isValidT6GridC(gridc),ILLEGAL_ARGUMENT);
    return setAtomOrDrop(gridc, atom);
  }
  
  bool GridManager::setAtomOrDrop(U16C gridc, const P4Atom atom) {
    T6Grid & g = *mT6GridPtr;
    if (!isAccessible(gridc)) return false;   // permanent-edge ring: dropped
    if (g.setAtomOrDrop(gridc,atom)) {
      bool b = mDLGridListPtr->pushFrontC(U8C(gridc.x,gridc.y));
      if (b) EACH(1'000'000,LOGPTAG(oldATOM,gridc));
      else EACH(1'000'000,LOGPTAG(newATOM,gridc));
      EACH(1'000'000,LOGPTAG(ATOMS,mDLGridListPtr->getLength()));
      return true;
    }
    return false;
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
      setAtomOrDrop(gridc,atom);
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
      ++mEWsReturned;           // tried to apply: committed + obsoleted
      if (matchesEW(ewt.mOld,center)) {
        SNAP(100,HBPTAG(ewaplid!,center));
        EACH(100'000,LOGPTAG(!MEW@,center));
        writeEW(ewt.mNew,center);
        ++mEWsCommitted;        // an event happened (AER counts these)
      } else {
        ++mEWsObsoleted;        // the window changed meanwhile: dropped
      }
    }

    // all EWPCs come through here
    EACH(100'000,HBPTAG(applyEWT-Seek,__EACHNUM__));
    if (!lgmc.isEPSuspReq() && seekRandomNonEmptySite(center)) {
      SNAP(10,HBNOTE(readEW));
      readEW(ewt.mOld,center);
      ++mEWsOffered;            // a real (non-empty) event shipped
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
