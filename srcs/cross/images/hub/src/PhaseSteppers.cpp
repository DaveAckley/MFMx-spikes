#include "PhaseSteppers.h"  //< for theL1CacheLoaders
#include "T6PhaseLockedLoop.h"

#include "HubLiveB.h"     //< for fB
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "CacheLoader.h"  //< for theL1CacheLoaders
#include "Printf.h"      //< for snprintf

namespace MFM {
  namespace PhaseSteppers {

    /* ATOM CENSUS (diagnostic).  Counts non-empty atoms in this tile's
       T6Grid by region, in full-grid coordinates:
         interior = self sites that follower events can touch (>= OVERLAP deep)
         band     = self sites in the outer OVERLAP ring (cache-exchanged)
         cache    = the OVERLAP-wide caches outside self
       Logged as dotted marks (always on; dotted non-.p tags ignore
       HBF_NO_UTAG):  .an/.ae:TOTAL:INTERIOR:BAND:CACHE   (hex)
                      .at:TYPE=COUNT,...  for types 2,4,5 and 'other'  (hex)
                      .ak:DLLEN:CMAX:CSUM  DL backlog, and max/sum of the type-5
                          event counter (mStg[1]): a frozen tile shows constant CMAX */
    struct GridCensus {
      u32 total, interior, band, cache;
      u32 type2, type4, type5, other;
      u32 dllen;                //< DLGridList backlog (changes not yet shipped to host)
      u32 cmax, csum;           //< max / wrapped sum of mStg[1] over type-5 atoms (liveness)
    };

    static void takeCensus(GridCensus & gc) {
      memset_s(&gc,'\0',sizeof(gc));
      T6Grid & g = fB.mGridManager.getT6GridOrDie();
      const u32 inLo = T6Grid::SELF_ORIGIN.x + NgbCacheMgr::OW;
      const u32 inHi = T6Grid::SELF_MAX.x - NgbCacheMgr::OW;
      const u32 jnLo = T6Grid::SELF_ORIGIN.y + NgbCacheMgr::OH;
      const u32 jnHi = T6Grid::SELF_MAX.y - NgbCacheMgr::OH;
      gc.dllen = theDLGridList.getLength();
      for (u32 x = 0; x < T6Grid::FULL_WIDTH; ++x) {
        for (u32 y = 0; y < T6Grid::FULL_HEIGHT; ++y) {
          const P4Atom a = g.getAtom(U16C(x,y));
          if (a.isEmpty()) continue;
          ++gc.total;
          const bool self =
            x >= T6Grid::SELF_ORIGIN.x && x < T6Grid::SELF_MAX.x &&
            y >= T6Grid::SELF_ORIGIN.y && y < T6Grid::SELF_MAX.y;
          if (!self) ++gc.cache;
          else if (x >= inLo && x < inHi && y >= jnLo && y < jnHi) ++gc.interior;
          else ++gc.band;
          switch (a.getType()) {
          case 2: ++gc.type2; break;
          case 4: ++gc.type4; break;
          case 5:
            ++gc.type5;
            if (a.mStg[1] > gc.cmax) gc.cmax = a.mStg[1];
            gc.csum += a.mStg[1];
            break;
          default: ++gc.other; break;
          }
        }
      }
    }

    static const char * fmtCensusCounts(Buf32 & b, const GridCensus & gc) {
      snprintf(b.buf,b.SIZE,"%x:%x:%x:%x",gc.total,gc.interior,gc.band,gc.cache);
      return b.buf;
    }

    static const char * fmtCensusLive(Buf32 & b, const GridCensus & gc) {
      snprintf(b.buf,b.SIZE,"%x:%x:%x",gc.dllen,gc.cmax,gc.csum);
      return b.buf;
    }

    static const char * fmtCensusTypes(Buf32 & b, const GridCensus & gc) {
      snprintf(b.buf,b.SIZE,"2=%x,4=%x,5=%x,o=%x",gc.type2,gc.type4,gc.type5,gc.other);
      return b.buf;
    }

    /* true when ready to ack "N<" (i.e., entering NEW LEADER) */
    bool workOnEnteringNewLeaderB() {
      CmdArg cda = thePLL.getIncomingCDA();
      s32 arg = cda.mArgs.mArg[0];

      DTSET(IHT,arg);

      DTRIG(IHT,{
          HBPTAG(BOLTR_iNL,arg);
          LOGPTAG(BOLTR_iNL,arg);
        });

      //// >>>> HACK TO SKIP FIRST SAVE CACHES <<<<
      {
        static bool once;
        if (!once) {
          once = true;
          //LOGPTAG(wOENLB,arg);
          return true; // lead: "all sent"; follow: "all rcvd"
        }
      }

      //// SAVE CACHES OF PRIOR LEADER
      DTRIG(IHT,{
          LOGPTAG(SAVE_CACHES,arg);
          HBPTAG(BOLTR_SAV$,(u32) arg);
        });
      theL1CacheLoaders.saveCaches();
      return theL1CacheLoaders.allReadyToAck();
    }

    bool workOnExitingNewLeaderB() {
      CmdArg cda = thePLL.getOutgoingCDA();
      s32 arg = cda.mArgs.mArg[0];
      HBASSERT_LT((u32)arg, 4);
      theL1CacheLoaders.configureCacheLoaders((u8) arg);
      theL1CacheLoaders.restartIterators();
      {                         // census after SAVE completed (true state)
        GridCensus gc; Buf32 b1, b2, b3;
        takeCensus(gc);
        LOGPTAG(.an,fmtCensusCounts(b1,gc));
        LOGPTAG(.at,fmtCensusTypes(b2,gc));
        LOGPTAG(.ak,fmtCensusLive(b3,gc));
      }
      HBPTAG(BOLTR_done_wOxNLB,cda.formatCmdArg());
      return true;              // all there is to it..
    }

    bool workOnEnteringRunEventsB() {
      //// LOAD CACHES OF CURRENT LEADER
      u32 arg = theL1CacheLoaders.getConfiguredSCL();
      if (false)
        EACH(100'000,{
          LOGPTAG(LODC$,arg);
          HBPTAG(BOLTR_LOD$,arg);
        });

      theL1CacheLoaders.loadCaches();
      return theL1CacheLoaders.allReadyToAck();
    }

    bool workOnExitingRunEventsB() {
      u32 arg = theL1CacheLoaders.getConfiguredSCL();
      LOGPTAG(woEXREB,arg);

      HBASSERT_LT((u32)arg, 4);

      if (true /* DONE DRAINING EWPS? */ ) {
        LOGPTAG(DRAIN_EWPS,arg);
        {                       // census at end of events, before SAVE
          GridCensus gc; Buf32 b1;
          takeCensus(gc);
          LOGPTAG(.ae,fmtCensusCounts(b1,gc));
        }
        theL1CacheLoaders.restartIterators();
        HBPTAG(BOLTR_done_wOxREB,(u32) arg);
        return true;              // done when EWPs done
      }
      return false;
    }

    extern "C" void assessProgressAllHarts() {
      
      EACH(100'000'000,LOGPTAG(BOLTR_AHS,__EACHNUM__));

      /////// CASES: 
      // ENTERING HOLD_AT_BIRTH
      //      if (thePLL.ifEntering(PBCmd::HOLD_AT_BIRTH)) {
      if (thePLL.ifCouldAck(PBCmd::HOLD_AT_BIRTH,Edge::ENTERING)) {
        thePLL.ackEntering(PBCmd::HOLD_AT_BIRTH);
        HBPTAG(VOTE!,thePLL.voteStatus());
      }

      // EXITING HOLD_AT_BIRTH
      if (thePLL.ifCouldAck(PBCmd::HOLD_AT_BIRTH,Edge::EXITING)) {
        thePLL.ackExiting(PBCmd::HOLD_AT_BIRTH);
        HBPTAG(VOTE!,thePLL.voteStatus());
        //LOGPTAG(XHAB!,thePLL.voteStatus());
      }

      // ENTERING NEW_LEADER
      if (thePLL.ifCouldAck(PBCmd::NEW_LEADER,Edge::ENTERING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnEnteringNewLeaderB()) {
          thePLL.ackEntering(PBCmd::NEW_LEADER);
          //          LOGPTAG(NNL!,thePLL.voteStatus());
        }
      }

      // EXITING NEW_LEADER
      if (thePLL.ifCouldAck(PBCmd::NEW_LEADER,Edge::EXITING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnExitingNewLeaderB()) {
          thePLL.ackExiting(PBCmd::NEW_LEADER);
          //LOGPTAG(XNL!,thePLL.voteStatus());
        }
      }

      // ENTERING RUN_EVENTS
      if (thePLL.ifCouldAck(PBCmd::RUN_EVENTS,Edge::ENTERING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnEnteringRunEventsB()) {
          thePLL.ackEntering(PBCmd::RUN_EVENTS);
          //          LOGPTAG(NRE!,thePLL.voteStatus());
        }
      }

      // EXITING RUN_EVENTS
      if (thePLL.ifCouldAck(PBCmd::RUN_EVENTS,Edge::EXITING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnExitingRunEventsB()) {
          thePLL.ackExiting(PBCmd::RUN_EVENTS);
          LOGPTAG(XRE!,thePLL.voteStatus());
        }
      }

    }

  }
}

