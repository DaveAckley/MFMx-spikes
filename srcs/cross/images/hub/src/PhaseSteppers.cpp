#include "PhaseSteppers.h"  //< for theL1CacheLoaders
#include "T6PhaseLockedLoop.h"

#include "HubLiveB.h"     //< for fB
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "CacheLoader.h"  //< for theL1CacheLoaders

namespace MFM {
  namespace PhaseSteppers {
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

