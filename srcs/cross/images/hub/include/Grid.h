#pragma once  /* -*- C++ -*- */
#include "T6Grid.h"
#include "HostBlock.h" // everyEventShipped() reads the host flags
#include "PT_Ewp.h"
#include "S16C.h"
#include "UxC.h" // For U16C
#include "FastT2.h" // for between(..)
#include "MDist.h"
#include "DemoGlobal.h"
#include "EP_ACacheBlock.h"
#include "EWLattice.h" // policy ewsnap: EW lattice geometry

namespace MFM {
  /* "NO AER CHEATING" MODE (run time, host flag HBF_EVERY_EVENT, set by the
     mfmxctl `events honest` command or --events honest; default shortcut).
     In shortcut mode a hub draws up to 100 random centers per event request,
     counts each EMPTY one as a free "act of physics" (mEWsEmptyCenters,
     credited to AER) and ships only the first occupied one.  In honest mode
     the hub draws ONE center, empty or not, packs the event window, ships it
     to an EWP, and the unchanged window comes back to be applied like any
     other: every event costs a full round trip, mEWsEmptyCenters stays 0,
     and the empty-grid AER inflation after the seed disappears.  The hubs
     log `.ee:1` (honest) or `.ee:0` next to `.cp`. */
  extern HostBlock theHostBlock;
  static inline bool everyEventShipped() {
    return (theHostBlock.mHostFlags & HostFlags::HBF_EVERY_EVENT) != 0u;
  }

#define LOGDL2D(sa,tag,d) do { d.log(GET_FILE_ID(__FILE__),__LINE__,sa,""#tag); } while (0)
  struct DL2D {
    using SitesArray = DL2D[DG::T6GRID_WIDTH][DG::T6GRID_HEIGHT];
    static constexpr U8C cNONE = U8C(U8_MAX,U8_MAX);

    U8C mNext, mPrev;
    bool isOccupied() const {
      if (mNext != cNONE && mPrev != cNONE) return true;
      MFM_API_ASSERT(mNext == cNONE && mPrev == cNONE,ILLEGAL_STATE);
      return false;
    }
    U8C getNextC() { return mNext; }
    U8C getPrevC() { return mPrev; }
    void setNextC(U8C c) { mNext = c; }
    void setPrevC(U8C c) { mPrev = c; }
    void setNone() { mNext = mPrev = cNONE; }
    void log() const {
      LOGPTAG(DL2D,this);
      LOGPTAG(DL2Dp,mPrev);
      LOGPTAG(DL2Dn,mNext);
    }
    void log(u16 fileid,u16 lineno,const SitesArray &sa,const char * tag) const {
      DL2D *base = (DL2D*) &sa;
      u32 dist = this - base; // DL2D distance
      U8C at(dist/DG::T6GRID_HEIGHT,dist%DG::T6GRID_HEIGHT);

      static constexpr u32 BUF_SIZ = 40;
      static char buf[BUF_SIZ];
      snprintf(buf,BUF_SIZ,"@%u,%u n:%u,%u p:%u,%u",
               at.x,at.y,
               mNext.x,mNext.y,
               mPrev.x,mPrev.y);
      markLogBlock(fileid,lineno,buf,tag);
    }

  };

  struct DLGridList {

    void logList() {
      LOGPTAG(DL2Dsol,mRoot);
      u32 count = 0;
      U8C last = DL2D::cNONE;
      if (mRoot != DL2D::cNONE) {
        U8C p = mRoot;
        do {
          DL2D & d = _get(p);
          LOGDL2D(mSites,DL2D=,d);
          ++count;
          if (last != DL2D::cNONE && d.getPrevC() != last) {
            LOGPTAG(DLGLlast,last);
            LOGPTAG(DLGLprev,d.getPrevC());
          }
          last = p;
          p = d.getNextC();
        } while (p != mRoot);
      }
      LOGPTAG(DLGLeol,count);
    }
    void demo() {
      LOGPTAG(DLGLdemo,sizeof(DLGridList));
      init();
      LOGPTAG(DLGLp12,pushFrontC({1,2}));
      LOGPTAG(DLGLp96,pushFrontC({9,6}));
      LOGPTAG(DLGLp37,pushFrontC({3,7}));
      logList();
      LOGPTAG(DLGLp12,pushFrontC({1,2}));
      logList();
      LOGPTAG(DLGLp37,pushFrontC({3,7}));
      logList();
      LOGPTAG(DLGLp00,pushFrontC({0,0}));
      logList();
      LOGPTAG(DLGLp00+,pushFrontC({0,0}));
      logList();
      LOGPTAG(DLGLp58,pushFrontC({5,8}));
      logList();
      U8C backc;
      while (popBackC(backc)) 
        LOGPTAG(DLGLpop,backc);
      LOGPTAG(DLGL~demo,this);
    }

    static bool isNoneC(U8C c) {
      return c == DL2D::cNONE;
    }
    static bool isValidC(U8C c) {
      return c.x < DG::T6GRID_WIDTH && c.y < DG::T6GRID_HEIGHT;
    }
    static void assertValidC(U8C c) {
      MFM_API_ASSERT(isValidC(c),ARRAY_INDEX_OUT_OF_BOUNDS);
    }

    u16 getLength() {
      AtomicScopeLock guard(mLock);
      return mLength;
    }

    /** Total sites popped for reporting since init (not counting
        re-queues); with getLength() this lets a mark show whether a
        nuke's backlog was drained to the host. */
    u32 getPopped() {
      AtomicScopeLock guard(mLock);
      return mPopped;
    }

    void init() ;

    bool popBackC(U8C & c) {
      AtomicScopeLock guard(mLock);
      if (mRoot == DL2D::cNONE) return false;
      U8C backc =_get(mRoot).getPrevC();
      _remove(backc,false);
      ++mPopped;
      c = backc;
      return true;
    }

    bool pushFrontC(U8C c) {
      AtomicScopeLock guard(mLock);
      DL2D & d = _get(c);
      //LOGDL2D(mSites,pFC,d);
      bool ret = false;
      if (d.isOccupied()) {
        if (oddsOf(14,15)) return true; // mostly don't move to front
        _remove(c,true);                // but occasionally do
        ret = true;             
      }
      // c is unlinked
      if (mRoot == DL2D::cNONE) { // list is empty
        d.setNextC(c);  // so point at self
        d.setPrevC(c);  // so point at self
        //LOGDL2D(mSites,nur,d);
      } else {
        DL2D & r = _get(mRoot);
        //LOGDL2D(mSites,olr,r);

        U8C oldnextc = r.getNextC();
        U8C oldprevc = r.getPrevC();

        d.setNextC(mRoot);      // our next is old root
        r.setPrevC(c);          // old root's prev is us
        if (oldnextc == mRoot)  // if old root's next was root
          r.setNextC(c);        // now it's us

        d.setPrevC(oldprevc);   // and our prev is old root prev
        if (oldprevc == mRoot)  // if old root's prev was root
          r.setPrevC(c);        // now it's us
        //LOGDL2D(mSites,fd,d);

        //LOGPTAG(opc,oldprevc);
        _get(oldprevc).setNextC(c); // and old tail's next is us
        //LOGDL2D(mSites,op,get(oldprevc));
      }
      mRoot = c;     // and we're the root either way
      ++mLength;
      //SNAP(10'000'000,LOGPTAG(mroo+,mLength));
      return ret;     // meaning moved to front (vs new insert)
    }

    bool removeForward(U8C c) {
      AtomicScopeLock guard(mLock);
      DL2D & d = _get(c);
      if (!d.isOccupied()) return false;
      _remove(c,true);
      return true;
    }

  private:
    static_assert((DG::T6GRID_WIDTH < U8_MAX),"T6GRID TOO WIDE");
    static_assert((DG::T6GRID_HEIGHT < U8_MAX),"T6GRID TOO HIGH");

    DL2D mSites[DG::T6GRID_WIDTH][DG::T6GRID_HEIGHT];

    AtomicLock mLock;
    U8C mRoot;
    u16 mLength;
    u32 mPopped;                // zeroed by init()

    //// PRIVATE _METHODS ASSUME LOCK IS HELD
    DL2D & _get(U8C c) {
      assertValidC(c);
      return mSites[c.x][c.y];
    }
    DL2D & _getPrev(DL2D & n) { return _get(n.getPrevC()); }
    DL2D & _getNext(DL2D & n) { return _get(n.getNextC()); }

    void _remove(U8C c, bool fromFront) {
      DL2D & d = _get(c);
      MFM_API_ASSERT(d.isOccupied(),ILLEGAL_ARGUMENT);
      if (mRoot == c) {
        U8C newroot = (fromFront ? d.getNextC() : d.getPrevC());
        mRoot = (newroot == mRoot) ? DL2D::cNONE : newroot;
      }
      U8C prevc = d.getPrevC();
      U8C nextc = d.getNextC();
      _get(prevc).setNextC(nextc);
      _get(nextc).setPrevC(prevc);
      d.setNone();
      MFM_API_ASSERT(mLength > 0,ILLEGAL_STATE);
      --mLength;
      //SNAP(10'000'000,LOGPTAG(mroo-,mLength));
    }
  };

  struct L1GridManagerControl {
    bool mEventProcessingSuspendRequest;     //< set by outside
    bool mEventProcessingSuspendRequestSeen; //< set by GridManager
    bool mEventProcessingSuspendStatus;      //< set by EP_ACacheBlock
    u8 mSuperCellLeader;                     //< set by outside
    u16 mReportEpoch;                        //< scy (low 16 bits), set by hb
    u8 mEventsPhase;                         //< 1 while events run (this epoch), else 0

    /* The epoch the atom reports are stamped with (AtomReport.h, epoch
       marker): hb sets it as a NEW_LEADER changeover begins, T0 reads it
       as it packs reports. */
    u16 getReportEpoch() const {
      memoryFence();
      return mReportEpoch;
    }

    void setReportEpoch(u16 val) {
      mReportEpoch = val;
      mEventsPhase = 0u;        // a new epoch starts in its SAVE/LOAD phase
      memoryFence();
    }

    /* CHANGE STAMP phase (AtomReport.h): 0 = SAVE/LOAD (transfers), 1 =
       events running.  hb sets 1 when the events start, and setReportEpoch
       clears it at the next NEW_LEADER, so a write is ordered (epoch,
       phase): SAVE/LOAD < events < the next epoch's SAVE/LOAD. */
    u8 getEventsPhase() const {
      memoryFence();
      return mEventsPhase;
    }
    void setEventsPhase(u8 val) {
      mEventsPhase = val;
      memoryFence();
    }

    u8 getSuperCellLeader() const {
      memoryFence();
      return mSuperCellLeader;  // U8_MAX == no leader?
    }

    void setSuperCellLeader(u8 val) {
      mSuperCellLeader = val;
      memoryFence();
    }

    bool isEPSuspReq() const {
      memoryFence();
      return mEventProcessingSuspendRequest;
    }
    bool isEPSuspReqSeen() const {
      memoryFence();
      return mEventProcessingSuspendRequestSeen;
    }
    bool isEPSuspStatus() const {
      memoryFence();
      return mEventProcessingSuspendStatus;
    }
    void setEPSuspReq(bool b) {
      mEventProcessingSuspendRequest = b;
      memoryFence();
    }
    void setEPSuspReqSeen(bool b) {
      mEventProcessingSuspendRequestSeen = b;
      memoryFence();
    }
    void setEPSuspStatus(bool b) {
      mEventProcessingSuspendStatus = b;
      memoryFence();
    }

    void init() {
      memset_s(this,0,sizeof(*this));
      //count on HOLD_AT_BIRTH instead? -- mEventProcessingSuspendStatus = true; // XXX hold at first positions?
      mSuperCellLeader = U8_MAX; // no SCL to start
    }
  };

  extern L1GridManagerControl theL1GridManagerControl;

  /* CHANGE STAMPS: when each site of the hub grid was last WRITTEN (any
     change, SELF or cache ring, reported or quiet): (epoch & 0x7fff) << 1
     | phase, see L1GridManagerControl.  The reporter sends it with the
     site's report (AtomReport::packStamp) so the host can order reports by
     when the site changed, not by when they were emitted. */
  extern u16 theSiteStamps[DG::T6GRID_WIDTH][DG::T6GRID_HEIGHT];

  /* POLICY EWSNAP / EWSNAP10 (EWLattice.h, branch diagonal-tiles): the
     live event centers of this hub per event area, built once at init.
     Index 0 = no corner yet (own tiles only), 1 + Corner4 (SE, SW, NW, NE) after a
     NEW_LEADER.  The scratch is used only while building. */
  static constexpr u32 CENTER_TABLES = 5u;
  extern EWLattice::CenterTable theCenterTables[CENTER_TABLES];
  extern u8 theCenterScratch[2u * EWLattice::bitBytes(DG::T6GRID_WIDTH,
                                                      DG::T6GRID_HEIGHT)];

  inline void stampSiteWritten(U16C gridc) {
    L1GridManagerControl & c = theL1GridManagerControl;
    theSiteStamps[gridc.x][gridc.y] =
      (u16) ((((u16) c.getReportEpoch() & 0x7fffu) << 1) | (c.getEventsPhase() & 1u));
  }

  /* The wire byte for site c, relative to the epoch `now` the stream last
     announced (the host reconstructs the change epoch from its own copy of
     it).  A stamp older than AtomReport::STAMP_MAX_AGE epochs is clamped to
     that age, a stamp from an epoch the stream has not announced yet reads
     as (now, events). */
  inline u8 siteStampByte(U8C c, u32 now) {
    const u16 st = theSiteStamps[c.x][c.y];
    const u16 nowe = (u16) (now & 0x7fffu);
    u16 stampe = (u16) (st >> 1);
    u16 phase = (u16) (st & 1u);
    const u16 age = (u16) ((nowe - stampe) & 0x7fffu);
    if (age >= 0x4000u) { stampe = nowe; phase = 1u; }      // from the future
    else if (age > AtomReport::STAMP_MAX_AGE) {
      stampe = (u16) ((nowe - AtomReport::STAMP_MAX_AGE) & 0x7fffu);
      phase = 0u;
    }
    return AtomReport::packStamp(stampe,phase);
  }

#define ALL_SUPERCELL_LEADER_STATUSES() \
    XX(NO_LEADER)                       \
    XX(WE_LEAD)                         \
    XX(WE_FOLLOW)                       \
    
  enum class SCStatus : u8 {
#define XX(name) name,
    ALL_SUPERCELL_LEADER_STATUSES()
#undef XX
  };

  constexpr const char * getSCStatusName(SCStatus s) {
    switch (s) {
#define XX(name) case SCStatus::name: return ""#name;
    ALL_SUPERCELL_LEADER_STATUSES()
#undef XX
    }
    return "ILLEGAL SCSTATUS";
  }

  struct NukeBlast;             // FORWARD (shared/include/NukeBlast.h)

  struct GridManager {
    static constexpr u8 MAX_NGBS = 8;
    
    void init(T6Grid & grid, ACacheBlockL1Control & acbl1, DLGridList & gridlist) ;

    T6Grid & getT6GridOrDie() {
      MFM_API_ASSERT_NONNULL(mT6GridPtr);
      return *mT6GridPtr;
    }

    U16C selectRandomSite(U16CRange bounds) ;

    /* A random event center: uniform in bounds, or (policy ewsnap)
       uniform over the live centers of the current corner's table. */
    U16C drawCenter(U16CRange bounds) ;

    /* Policy ewsnap/ewsnap10: build theCenterTables (ENGINE_FAIL if a
       table does not fit). */
    void buildCenterTables() ;

    template <class L>
    void buildCenterTablesT() ;

    template <class L>
    bool ownsSiteT(U16C gridc) const ;

    /* Global site of our grid's (0,0). */
    EWLattice::Pt localOrigin() const {
      return EWLattice::Pt{
        mGlobalOriginX - (s32) DG::T6GRID_OVERLAP_WIDTH,
        mGlobalOriginY - (s32) DG::T6GRID_OVERLAP_HEIGHT};
    }

    /* Is grid site gridc ours, i.e. reported by us when it changes: in
       our SELF rectangle, or (policy ewsnap/ewsnap10) in a tile whose
       center is in our hub rectangle. */
    bool ownsSite(U16C gridc) const ;

    S16C siteNumberToOffset(u32 sn) const {
      const MDist4 md;
      SPoint sp = md.getPoint(sn);
      return S16C(sp);
    }

    SCStatus leadFollowOrGetOutOfTheWay() const ;
    
    bool seekRandomNonEmptySite(U16C & found) ;

    /* Sites in the cache ring of a PERMANENT array edge (HubEdges.h) are
       INACCESSIBLE: events read INACCESSIBLE_TYPE there, writes are
       dropped, so those caches stay EMPTY from init and never change. */
    bool isAccessible(U16C gridc) const ;
    P4Atom getAtomOrInaccessible(U16C gridc) const ;

    bool setAtom(U16C gridc, const P4Atom a) ;
    bool setAtomOrDrop(U16C gridc, const P4Atom a) ;

    /* Write the grid but do NOT queue the site for the host display.  For
       cache load/save (CacheLoader::unloadPacket): the pixel already shows
       this value (the borrower's last painted value, or the lender's), and
       if both the borrower and the lender painted it, a stale copy could
       overwrite the owner's.  Events (writeEW) still paint as usual.
       Exception (2026-11-24): CacheLoader::unloadPacket now uses
       setAtomOrDrop for sites inside this hub's SELF region (a follower's
       SAVE), because a SAVE that empties a SELF site otherwise left the
       host with a stale pixel (turds, all within the overlap band). */
    bool setAtomQuiet(U16C gridc, const P4Atom a) ;

    bool matchesEW(const EventWindow & ew,U16C center) const ;

    void readEW(EventWindow & ew,U16C center) ;

    u32 writeEW(const EventWindow & ew,U16C center) ;

    /* NUKE: erase (set EMPTY) every non-empty site of the WHOLE T6Grid,
       caches included, whose global site coordinate is inside the blast
       (NukeBlast.h), through setAtomOrDrop so the host display sees it.
       Only valid with events stopped (PhaseSteppers: exit of NEW_LEADER).
       Returns the number of SELF sites erased; cache sites erased are added
       to `cacheErased`. */
    u32 applyNuke(const NukeBlast & blast, u32 & cacheErased) ;

    void applyEWT(EwpPayload &ewt, u32 ngbidx) ;

    u8 getOurSuperCellPosition() const {
      return mSuperCellLeaderCode;
    }

    u8 mPermEdges;              //< HubEdges::PE_* sides with no neighbor hub
    s32 mGlobalOriginX;         //< global site x of our first SELF site (NukeBlast.h)
    s32 mGlobalOriginY;         //< global site y of our first SELF site
    s32 mHubX;                  //< global hub column (EWLattice.h)
    s32 mHubY;                  //< global hub row
    bool mEWSnap;               //< an EW-lattice policy (ewsnap*)
    u8 mEWTile;                 //< its tile radius: 4 or 10 (else 0)
    u32 mCenterLive[CENTER_TABLES]; //< ewsnap: live centers per table
    DLGridList * mDLGridListPtr;
    T6Grid * mT6GridPtr;
    ACacheBlockL1Control * mACBL1Ctrl;
    u32 mAutoseedWaitCount;
    u32 mEWsEmptiesShipped;
    u32 mTotalAtomicChanges;
    /* Event counters, 64 bits: they only grow (AER, scripts/aer/aer.py).
       Every random site the hub draws is an event: one at an empty site
       is "an act of physics" that does nothing (mEWsEmptyCenters), one
       at an occupied site is shipped to an EWP (mEWsOffered) and comes
       back (mEWsReturned) to be committed or found obsolete. */
    u64 mEWsOffered;
    u64 mEWsReturned;
    u64 mEWsCommitted;
    u64 mEWsObsoleted;
    u64 mEWsEmptyCenters;
    /* Every non-empty EWT shipped comes back exactly once, and each return
       lands in exactly one of COMMITTED, OBSOLETED (window changed), STALE
       (shipped in an earlier epoch: the EWP only runs during RUN_EVENTS, so
       one in flight at the exit waits there and returns in a later epoch,
       when its window may be on sites now lent to a leader; it is dropped
       unread) or UNAPPLIED (EWs suspended, or it came back without a
       result).  So RETURNED = COMMITTED + OBSOLETED + STALE + UNAPPLIED and
       OUTSTANDING = OFFERED - RETURNED is the number still out there. */
    u64 mEWsStale;
    u64 mEWsUnapplied;
    u64 getEWsOutstanding() const { return mEWsOffered - mEWsReturned; }
    u8 mEmptiesRun[MAX_NGBS];
    u8 mSuperCellLeaderCode;  //< if there is a SCL, it's us if it's this
  };

  extern DLGridList theDLGridList; // defined in hub/LiveB.cpp
}
