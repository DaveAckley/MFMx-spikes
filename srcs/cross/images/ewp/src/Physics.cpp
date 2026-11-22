#include "Physics.h"
#include "FastT2.h" // for oneIn, between, etc
#include "HostBlock.h"
#include "FastLocal.h" // for fAll

namespace MFM {

  extern HostBlock theHostBlock;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  static constexpr u32 PHY_BOOM = 6u;   // self-test: scribble, then FAIL

  bool Physics::doTransition(EwpPayload & ewt) {
    bool ret = false;
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_ONLY) {
      ++mEWsAttempted;
      ewL1ToFast(ewt.mOld);
      GuardedResult gr = guardedPhysics();
      if (gr == GR_TRUE) {
        ret = true;
        ewFastToL1(ewt.mNew);
        ++mEWsSucceeded;
      } else {
        // Physics said no, or failed part way: commit the PRE-event window
        // (mOld is never written), so a half-modified mFastEW never lands.
        memcpy((void*) &ewt.mNew, (const void*) &ewt.mOld, sizeof(EventWindow));
        ++mEWsFailed;
        if (gr == GR_JUMPED) {
          u32 cat = ewt.mOld.mAtoms[0].getType();
          // The MFM response to a failing event: erase the center and leave
          // the rest of the window as it was.
          ewt.mNew.mAtoms[0] = P4Atom::makeEmptyAtom();
          ++mEWsPhysFailed;
          if (mEWsPhysFailed <= 8u || (mEWsPhysFailed & 1023u) == 0u) {
            // (code+100) is FailCodes' enum value counted from DUMMY, then
            // the center type in the low 9 bits.
            u32 pf = (((u32) (mRecover.mCode + 100)) << 9) | cat;
            u32 pw = (((u32) mRecover.mFileId) << 16) | mRecover.mLine;
            LOGPTAG(.pf,pf);
            HBPTAG(.pf,pf);
            LOGPTAG(.pw,pw);    // where: (file id << 16) | line
            HBPTAG(.pw,pw);
            HBPTAG(PHYSFAIL,mEWsPhysFailed);
          }
        }
      }
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_AND_DEST;

      if (mEWsAttempted%100'000 == 0) {
        HBPTAG(pewAt,mEWsAttempted);
        HBPTAG(pewSu,mEWsSucceeded);
        HBPTAG(pewPF,mEWsPhysFailed);
      }
    } else if (ewt.mPayloadState.mPayloadCode != EwpPayloadCode::EWPC_EMPTY) {
      HBPTAG(wotwot?,ewt.mPayloadState.mPayloadCode);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_EMPTY;
    } 
    return ret;
  }

  /* The one setjmp per event.  Small and noinline on purpose: HB stack is
     precious, and this frame is all the guard costs.  Nothing here is read
     after the jump except `this` (unmodified, so it survives in a callee-saved
     register) and members. */
  __attribute__((noinline))
  Physics::GuardedResult Physics::guardedPhysics() {
    if (RECOVER_SETJMP(mRecover.mJB) == 0) {
      fAll.mRecover = &mRecover;        // FAILs on THIS hart now land here
      bool r = doFastPhysics();
      fAll.mRecover = 0;
      return r ? GR_TRUE : GR_FALSE;
    }
    return GR_JUMPED;           // PrepareToDie cleared fAll.mRecover, set mCode
  }

  bool Physics::doFastPhysics() {
    HostBlock & hb = theHostBlock;
    EventWindow & ew = mFastEW;
    P4Atom & ca = ew.mAtoms[0];
    u32 cat = ca.getType();

    SNAP(50,HBXTAG(FIZIX,cat));

    ////// BEGIN "PHYSICS" //////
    switch (cat) {
    default: {
      // wth are you? 
      ca = P4Atom::makeEmptyAtom(); // buhbye now
      return true;
    }
    case P4Atom::EMPTY_TYPE: return true;
    case P4Atom::INACCESSIBLE_TYPE:
      // The hub must never dispatch an event centered on a site that does
      // not exist: the engine is broken, so ENGINE_FAIL (never recoverable).
      ENGINE_FAIL(ILLEGAL_STATE);

    case P4Atom::START_TYPE: {
      //DP.printf("IAMI P4Atom::START_TYPE@0x%x\n",(u32) &ca);
      if (hb.mCommonArgs[1] != U16_MAX)
        ca = P4Atom::makeAtom((u16) hb.mCommonArgs[1]);
      else 
        ca = P4Atom::makeAtom(PHY_DREG);
      HBXTAG(STARTZO,ca.getType());
      return true;
    }

    case PHY_FB1: {
      ca.mStg[1]++;             // cheat and access underlying u32
      if (ca.mStg[0] > 0) {
        if (--ca.mStg[0] == 0) {
          for (u32 ngbsn = 0u; ngbsn <= 40; ++ngbsn) { 
            if (oneIn(100)) continue; // 1% survives
            ew.mAtoms[ngbsn] = P4Atom::makeEmptyAtom(); // kabam
          }
          return true;
        } else ew.mAtoms[between(5u,8u)] = ca; // extra repro shot (Moore!)
      } else if (ca.mStg[1] >= 21 && oneIn(100'000)) { // old enough to drink..
        EACH(1,LOGPTAG(SPLAT!,__EACHNUM__));
        ca.mStg[0] = between(5,500);
        ca.mStg[1] += between(1'000,10'000); // jump in color space too
      }

      u32 ngbsn = between(1u,4u);
      ew.mAtoms[ngbsn] = ca;
      //      sleepCycles(1'000'000);   // waste time to simulate more complex physics
      return true;
    }

    case PHY_FB4: {
      ca.mStg[1]++;             // cheat and access underlying u32
      EACH(1'000'000,HBPTAG(FB4,ca.mStg[1]));
      for (u32 ngbsn = 1u; ngbsn <= 40u; ++ngbsn) { //MAX FB!
        P4Atom & na = ew.mAtoms[ngbsn];
        na = ca; // kaboom
      }
      return true;
    }

    case PHY_DREG: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      u32 natype = na.getType();
      if (natype == P4Atom::INACCESSIBLE_TYPE) {
        // DO NOTHING! DON'T GO THERE!
      } else if (natype == P4Atom::EMPTY_TYPE) {
        if (oneIn(250u)) na = ca; // New me!
        else if (oneIn(25u)) na = P4Atom::makeAtom(PHY_RES);
        else ew.swap(0u,ngbsn);
      } else if (natype == PHY_DREG) { // dreg on dreg battle
        if (oneIn(20u)) na = P4Atom::makeEmptyAtom();
      } else if (oneIn(40u)) { // general destruction
        na = P4Atom::makeEmptyAtom();
        if (!ew.swap(0u,ngbsn))
          FAIL(USER_REQUESTED_FAILURE); // XXX use swap retval
      } 
      return true;
    }
      
    case PHY_BOOM: {
      // Self-test for physics-failure recovery (seed with 'decay 6'): spread
      // over the neighbors, then fail.  Recovery must commit the OLD window
      // with this center erased, so the neighbors show no trace of it.
      for (u32 ngbsn = 1u; ngbsn <= 4u; ++ngbsn) ew.mAtoms[ngbsn] = ca;
      FAIL(USER_REQUESTED_FAILURE);
    }

    case PHY_RES: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      if (na.getType() == P4Atom::EMPTY_TYPE)
        ew.swap(0u,ngbsn);
      return true;
    }
    }
    //// END OF "PHYSICS" ////
    
    return false; // NOT REACHED
  }
}
