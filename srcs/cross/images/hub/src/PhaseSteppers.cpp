#include "PhaseSteppers.h"  //< for theL1CacheLoaders
#include "T6PhaseLockedLoop.h"

#include "HubLiveB.h"     //< for fB
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "CacheLoader.h"  //< for theL1CacheLoaders

namespace MFM {
  namespace PhaseSteppers {
    bool workOnEnteringNewLeaderB() {
      CmdArg cda = thePLL.getIncomingCDA();
      s32 arg = cda.mArgs.mArg[0];

      HBPTAG(BOLTR_iNEWLEADER,arg);
      LOGPTAG(BOLTR_iNEWLEADER,arg);

      //// >>>> HACK TO SKIP FIRST SAVE CACHES <<<<
      {
        static bool once;
        if (!once) {
          once = true;
          LOGPTAG(BOLTR_BIGSHORT,arg);
          return true;
        }
      }

      //// SAVE CACHES
      LOGPTAG(SAVE_CACHES,arg);
      //      FAIL(INCOMPLETE_CODE);
      return true;
    }

    bool workOnExitingNewLeaderB() {
      CmdArg cda = thePLL.getCurrentCDA();
      s32 arg = cda.mArgs.mArg[0];
      HBASSERT_LT((u32)arg, 4);
      theL1CacheLoaders.configureCacheLoaders((u8) arg);
      return true;              // all there is to it..
    }

    extern "C" void assessProgressAllHarts() {
      
      EACH(100'000'000,LOGPTAG(BOLTR_AHS,__EACHNUM__));

      /////// CASES: 
      // ENTERING HOLD_AT_BIRTH
      //      if (thePLL.ifEntering(PBCmd::HOLD_AT_BIRTH)) {
      if (thePLL.ifCouldAck(PBCmd::HOLD_AT_BIRTH,Edge::ENTERING)) {
        thePLL.doneEntering(PBCmd::HOLD_AT_BIRTH);
        HBPTAG(VOTE!,thePLL.voteStatus());
      }

      // EXITING HOLD_AT_BIRTH
      if (thePLL.ifCouldAck(PBCmd::HOLD_AT_BIRTH,Edge::EXITING)) {
        thePLL.doneExiting(PBCmd::HOLD_AT_BIRTH);
        HBPTAG(VOTE!,thePLL.voteStatus());
      }

      // ENTERING NEW_LEADER
      if (thePLL.ifCouldAck(PBCmd::NEW_LEADER,Edge::ENTERING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnEnteringNewLeaderB()) {
          thePLL.doneEntering(PBCmd::NEW_LEADER);
          HBPTAG(VOTE!,thePLL.voteStatus());
        }
      }

      // EXITING NEW_LEADER
      if (thePLL.ifCouldAck(PBCmd::NEW_LEADER,Edge::EXITING)) {
        if (!onHartNum(HARTNUM_B) || PhaseSteppers::workOnExitingNewLeaderB()) {
          thePLL.doneExiting(PBCmd::NEW_LEADER);
          HBPTAG(VOTE!,thePLL.voteStatus());
        }
      }
    }



#if 0
      bool imageDoneRespondingToBoltB(PhaserBolt & pb) {
        MFM_API_ASSERT_ON_HART(HARTNUM_B);
        if (pb.getCmd() == PhaserBolt::Cmd::NEW_LEADER) {
          s32 arg;
          if (pb.getBoltDataWordIfAny(0,arg) && arg >= 0 && arg < 256)
            return bRespondToCMD_NEW_LEADER(arg);
          FAIL(OUT_OF_BOUNDS);
        }

        FAIL(INCOMPLETE_CODE);
        /*
          if (pb.getCmd() == PhaserBolt::Cmd::LOAD_CACHE) {
          //// DELEGATE HANDLING+ACK TO IHPC::updateCars
          return thePLL.weHaveAcknowledgedCurrentBolt();
          }
        */

        return true; // done either way.
      }
      bool imageDoneRespondingToBoltT0(PhaserBolt & pb) { return true; }
      bool imageDoneRespondingToBoltT1(PhaserBolt & pb) { return true; }
      bool imageDoneRespondingToBoltT2(PhaserBolt & pb) { return true; }
      bool imageDoneRespondingToBoltNC(PhaserBolt & pb) { return true; }
#endif
  }
}

