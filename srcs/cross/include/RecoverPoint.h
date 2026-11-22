#pragma once    /* -*- C++ -*- */

/* RecoverPoint -- let a FAIL jump back to a known place instead of hanging.
 *
 * FAIL() ends in PrepareToDie(), which (when a hart has a RecoverPoint
 * installed in its own private fAll.mRecover) records the failure in that
 * RecoverPoint and longjmps to it, instead of writing the HostBlock fail
 * record and hanging the hart.  ENGINE_FAIL() skips that and always hangs:
 * use it for "the engine itself is broken" (e.g. the hub dispatched an event
 * on a site that does not exist).  fAll is private to each hart, so the
 * mechanism is per hart by construction; each user owns its own RecoverPoint.
 *
 * First user: Physics::guardedPhysics on HB (one setjmp per event).  Cost
 * (measured on a T6 BRISC, ERISC_NOTES 9.22): setjmp ~19 wall-clock ticks,
 * setjmp+longjmp ~50.  The buffer is 14 words (ra, sp, s0..s11), not on the
 * stack: put the RecoverPoint in a long-lived object.
 *
 * Rules for code that can be jumped out of: no destructors, held locks or
 * half-built shared state in the frames between the install and the failure;
 * locals read after the jump must be volatile.  Engine services with shared
 * state (marks, logging, ...) wrap themselves in RecoverSuspend so a FAIL
 * inside them still hangs.  Does not cover wild pointers, illegal
 * instructions or infinite loops.
 */

#include "itype.h"          // u16, u32, s32

#ifdef RECOVER_HOSTTEST
#include <setjmp.h>
#define RECOVER_SETJMP(jb) setjmp(jb)
#define RECOVER_LONGJMP(jb, v) longjmp((jb), (v))
#else
#define RECOVER_SETJMP(jb) MFM::recSetjmp(jb)
#define RECOVER_LONGJMP(jb, v) MFM::recLongjmp((jb), (v))
#endif

namespace MFM {
#ifdef RECOVER_HOSTTEST
  typedef jmp_buf RecoverJB;
#else
  typedef u32 RecoverJB[14];    // ra, sp, s0..s11
  // C linkage names (no namespace in the symbol), defined in FATAL.cpp
  extern "C" int recSetjmp(u32 * jb) __attribute__((returns_twice));
  extern "C" void recLongjmp(u32 * jb, int v) __attribute__((noreturn));
#endif

  struct RecoverPoint {
    RecoverJB mJB;
    s32 mCode;                  //< FAILCode of the failure we caught
    u16 mFileId;                //< file id of the failing site (0 if none)
    u16 mLine;                  //< line of the failing site
  };

  /* If `slot` holds a RecoverPoint: record the failure in it, clear `slot`
     (this hart's own fAll.mRecover) and jump; does not return.  If `slot` is
     null: returns, and the caller goes on to hang as before. */
  inline void recoverNow(RecoverPoint * & slot, s32 code, u32 fileId,
                         u32 line) {
    RecoverPoint * rp = slot;
    if (rp == 0) return;
    rp->mCode = code;
    rp->mFileId = (u16) fileId;
    rp->mLine = (u16) line;
    slot = 0;
    RECOVER_LONGJMP(rp->mJB, 1);
  }

  /* Suspend recovery on this hart for a scope: a FAIL inside it hangs.  For
     engine services that must not be abandoned half way. */
  struct RecoverSuspend {
    RecoverPoint * & mSlot;
    RecoverPoint * mSaved;
    explicit RecoverSuspend(RecoverPoint * & slot) : mSlot(slot), mSaved(slot) {
      mSlot = 0;
    }
    ~RecoverSuspend() { mSlot = mSaved; }
  };
}
