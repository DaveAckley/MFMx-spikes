#include "Physics.h"
#include "FastT2.h" // for oneIn, between, etc
#include "HostBlock.h"

/* setjmp / longjmp for the T6 RISC-V hart: ra, sp, s0..s11 (14 words, no FP
   on this core).  Same code as t6pp/t6sjlj.c, proven on a T6 BRISC (7/7 tests,
   ERISC_NOTES 9.22).  No libc is linked into the images. */
__asm__(".pushsection .text,\"ax\"\n"
        ".globl physSetjmp\n.type physSetjmp,@function\n"
        "physSetjmp:\n"
        "  sw ra, 0(a0)\n  sw sp, 4(a0)\n"
        "  sw s0, 8(a0)\n  sw s1, 12(a0)\n  sw s2, 16(a0)\n  sw s3, 20(a0)\n"
        "  sw s4, 24(a0)\n  sw s5, 28(a0)\n  sw s6, 32(a0)\n  sw s7, 36(a0)\n"
        "  sw s8, 40(a0)\n  sw s9, 44(a0)\n  sw s10, 48(a0)\n  sw s11, 52(a0)\n"
        "  li a0, 0\n  ret\n"
        ".size physSetjmp,.-physSetjmp\n"
        ".globl physLongjmp\n.type physLongjmp,@function\n"
        "physLongjmp:\n"
        "  lw ra, 0(a0)\n  lw sp, 4(a0)\n"
        "  lw s0, 8(a0)\n  lw s1, 12(a0)\n  lw s2, 16(a0)\n  lw s3, 20(a0)\n"
        "  lw s4, 24(a0)\n  lw s5, 28(a0)\n  lw s6, 32(a0)\n  lw s7, 36(a0)\n"
        "  lw s8, 40(a0)\n  lw s9, 44(a0)\n  lw s10, 48(a0)\n  lw s11, 52(a0)\n"
        "  mv a0, a1\n  bnez a0, 1f\n  li a0, 1\n1: ret\n"
        ".size physLongjmp,.-physLongjmp\n"
        ".popsection\n");

namespace MFM {

  extern HostBlock theHostBlock;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  static constexpr u32 PHY_BOOM = 6u;   // self-test: scribble, then PHYSICS_FAIL

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
          // the rest of the window as it was.  (An INACCESSIBLE center stays.)
          if (cat != P4Atom::INACCESSIBLE_TYPE)
            ewt.mNew.mAtoms[0] = P4Atom::makeEmptyAtom();
          ++mEWsPhysFailed;
          if (mEWsPhysFailed <= 8u || (mEWsPhysFailed & 1023u) == 0u) {
            // (code+100) is FailCodes' enum value counted from DUMMY, then
            // the center type in the low 9 bits.
            u32 pf = (((u32) (mGuard.mCode + 100)) << 9) | cat;
            LOGPTAG(.pf,pf);
            HBPTAG(.pf,pf);
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
    if (PHYSGUARD_SETJMP(mGuard.mJB) == 0) {
      mGuard.arm();
      bool r = doFastPhysics();
      mGuard.disarm();
      return r ? GR_TRUE : GR_FALSE;
    }
    return GR_JUMPED;           // PHYSICS_FAIL disarmed and recorded mCode
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
    case P4Atom::INACCESSIBLE_TYPE: return false;

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
          PHYSICS_FAIL(USER_REQUESTED_FAILURE); // XXX use swap retval
      } 
      return true;
    }
      
    case PHY_BOOM: {
      // Self-test for physics-failure recovery (seed with 'decay 6'): spread
      // over the neighbors, then fail.  Recovery must commit the OLD window
      // with this center erased, so the neighbors show no trace of it.
      for (u32 ngbsn = 1u; ngbsn <= 4u; ++ngbsn) ew.mAtoms[ngbsn] = ca;
      PHYSICS_FAIL(USER_REQUESTED_FAILURE);
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
