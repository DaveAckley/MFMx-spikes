#include "Physics.h"
#include "FastT2.h" // for oneIn, between, etc
#include "HostBlock.h"
#include "FastLocal.h" // for fAll
#include "NativeFB.h"  // native FB1/FB4, shared with the host test
#include "BytePrograms.h" // bytecode FB1/FB4 (scripts/bytephys)
#include "Printf.h"    // for snprintf

namespace MFM {

  extern HostBlock theHostBlock;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  static constexpr u32 PHY_BOOM = 6u;   // self-test: scribble, then FAIL

  // Bytecode spike (OPEN_ISSUES 'physics as bytecode'): FB1 and FB4 run
  // by the BytePhys machine, so 'decay 7' vs 'decay 4' compares them.
  static constexpr u32 PHY_BFB1 = 7u;
  static constexpr u32 PHY_BFB4 = 8u;
  static constexpr u32 BP_FUEL = 10'000u; // instructions per event

  // The tile's wall clock, AI clock cycles (as FastT0.cpp reads it); the
  // low word is enough for one event.
  static const volatile u32 * PT_WALL_CLOCK_LO =
    (volatile u32 *) 0xffb1'21f0u;
  static constexpr u32 PT_EVENTS_PER_MARK = 1u << 16;

  /* What NativeFB.h and BytePhys.h need from the EWP. */
  struct EwpPhysEnv {
    EventWindow & mEW;

    P4Atom & atom(u32 sn) { return mEW.mAtoms[sn]; }
    u32 between(u32 lo, u32 hi) { return MFM::between(lo, hi); }
    bool oneIn(u32 n) { return MFM::oneIn(n); }
    void splat() { EACH(1,LOGPTAG(SPLAT!,__EACHNUM__)); }

    /* A bytecode machine fault: FAIL into the physics RecoverPoint (one
       FAIL line per kind, so the `.pw` mark's line number names it). */
    [[noreturn]] void fault(u32 f) {
      switch (f) {
      case BytePhys::BF_STACK_OVER: FAIL(OUT_OF_ROOM);
      case BytePhys::BF_STACK_UNDER: FAIL(ILLEGAL_STATE);
      case BytePhys::BF_CALL_OVER: FAIL(OUT_OF_RESOURCES);
      case BytePhys::BF_CALL_UNDER: FAIL(INCONSISTENT_STATE);
      case BytePhys::BF_OPCODE: FAIL(UNSUPPORTED_OPERATION);
      case BytePhys::BF_PC: FAIL(OUT_OF_BOUNDS);
      case BytePhys::BF_SITE: FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
      case BytePhys::BF_FIELD: FAIL(BAD_VALUE);
      case BytePhys::BF_FUEL: FAIL(OUT_OF_RESOURCES);
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }
  };

  /* Slot of a timed type, or PT_SLOTS if not timed. */
  static u32 ptSlot(u32 type) {
    return type == PHY_FB1 ? 0u
         : type == PHY_FB4 ? 1u
         : type == PHY_BFB1 ? 2u
         : type == PHY_BFB4 ? 3u
         : (u32) Physics::PT_SLOTS;
  }

  /* `.pt:TYPE:EVENTS:TICKS:INSNS.` (hex) per EWP and timed type, every
     PT_EVENTS_PER_MARK events of that type (or before TICKS could
     overflow): TICKS = AI clock cycles spent in guardedPhysics, INSNS =
     bytecode instructions (0 for native).  runsum.py sums them over all
     tiles: mean cycles per event, native vs bytecode. */
  void Physics::notePhysicsTime(u32 type, u32 ticks) {
    const u32 s = ptSlot(type);
    if (s >= PT_SLOTS) return;
    ++mPtEvents[s];
    mPtTicks[s] += ticks;
    if (s >= 2u) mPtInsns[s] += mVM.mExecuted;
    if (mPtEvents[s] >= PT_EVENTS_PER_MARK || mPtTicks[s] >= (1u << 31)) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%x:%x:%x:%x.", (unsigned) type,
               (unsigned) mPtEvents[s], (unsigned) mPtTicks[s],
               (unsigned) mPtInsns[s]);
      LOGPTAG(.pt,buf);
      HBPTAG(.pt,buf);
      mPtEvents[s] = 0u;
      mPtTicks[s] = 0u;
      mPtInsns[s] = 0u;
    }
  }

  bool Physics::doTransition(EwpPayload & ewt) {
    bool ret = false;
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_ONLY) {
      ++mEWsAttempted;
      ewL1ToFast(ewt.mOld);
      const u32 ptType = mFastEW.mAtoms[0].getType();
      const u32 ptStart = *PT_WALL_CLOCK_LO;
      GuardedResult gr = guardedPhysics();
      notePhysicsTime(ptType, *PT_WALL_CLOCK_LO - ptStart);
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
          // A failure with the stack at (or within 64 bytes of) its limit may
          // be a symptom of stack corruption, not a physics bug: do not
          // recover from that.  (Checked on recovery only: rare.)
          if (estimateStackHeadroom() < 64u) ENGINE_FAIL(OUT_OF_ROOM);
          if (mEWsPhysFailed <= 8u || (mEWsPhysFailed & 1023u) == 0u) {
            // (code+100) is FailCodes' enum value counted from DUMMY, then
            // the center type in the low 9 bits.
            u32 pf = (((u32) (mRecover.mCode + 100)) << 9) | cat;
            u32 pw = (((u32) mRecover.mFileId) << 16) | mRecover.mLine;
            LOGPTAG(.pf,pf);
            HBPTAG(.pf,pf);
            LOGPTAG(.pw,pw);    // where: (file id << 16) | line
            HBPTAG(.pw,pw);
            // Stack high-water mark (bytes) and headroom left above the
            // data (the whole free stack is canary-filled at boot, _BUD.S).
            u32 sk = estimateStackUsage();
            u32 sh = estimateStackHeadroom();
            LOGPTAG(.sk,sk);
            HBPTAG(.sk,sk);
            LOGPTAG(.sh,sh);
            HBPTAG(.sh,sh);
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

    case PHY_FB1: {             // native (NativeFB.h)
      EwpPhysEnv env{ew};
      return nativeFB1(env);
    }

    case PHY_FB4: {             // native (NativeFB.h)
      EwpPhysEnv env{ew};
      const bool r = nativeFB4(env);
      EACH(1'000'000,HBPTAG(FB4,ca.mStg[1]));
      return r;
    }

    case PHY_BFB1: {            // bytecode (scripts/bytephys/fb1.bp)
      EwpPhysEnv env{ew};
      return mVM.run(BytePhys::BP_PROG_FB1, env, BP_FUEL);
    }

    case PHY_BFB4: {            // bytecode (scripts/bytephys/fb4.bp)
      EwpPhysEnv env{ew};
      return mVM.run(BytePhys::BP_PROG_FB4, env, BP_FUEL);
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
