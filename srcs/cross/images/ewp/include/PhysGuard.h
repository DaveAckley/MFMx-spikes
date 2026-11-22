#pragma once    /* -*- C++ -*- */

/* PhysGuard -- recover from a failure INSIDE the physics of one event.
 *
 * FAIL() wedges the hart for good (BHANG) and is for real bugs.  Physics code
 * instead calls PHYSICS_FAIL(code) (inside a Physics member): if the event is
 * running under a guard, that records the code and longjmps back to the
 * guard, and Physics::doTransition then commits the pre-event window (mOld)
 * with the center atom erased.  Outside a guard it is exactly FAIL(code), so
 * a stray PHYSICS_FAIL can never silently return.
 *
 * Cost (measured on a T6 BRISC, ERISC_NOTES 9.22): setjmp ~19 wall-clock
 * ticks, setjmp+longjmp ~50.  Stack and L1 are precious on HB: the buffer is
 * 14 words (ra, sp, s0..s11) living in the one Physics object, not on the
 * stack; the setjmp frame is the small noinline Physics::guardedPhysics.
 *
 * Rules for code that can be jumped out of: no destructors, held locks or
 * half-built shared state in the frames between the guard and the failure;
 * locals read after the jump must be volatile (there are none today).  Does
 * not cover wild pointers, illegal instructions or infinite loops.
 */

#include "itype.h"          // u32, s32, uv32, sv32

#ifdef PHYSGUARD_HOSTTEST
#include <setjmp.h>
#define PHYSGUARD_SETJMP(jb) setjmp(jb)
#define PHYSGUARD_LONGJMP(jb, v) longjmp((jb), (v))
#else
#include "Fail.h"
#define PHYSGUARD_SETJMP(jb) MFM::physSetjmp(jb)
#define PHYSGUARD_LONGJMP(jb, v) MFM::physLongjmp((jb), (v))
#endif

namespace MFM {
#ifdef PHYSGUARD_HOSTTEST
  typedef jmp_buf PhysJB;
#else
  typedef u32 PhysJB[14];       // ra, sp, s0..s11
  // C linkage names (no namespace in the symbol), defined in Physics.cpp
  extern "C" int physSetjmp(u32 * jb) __attribute__((returns_twice));
  extern "C" void physLongjmp(u32 * jb, int v) __attribute__((noreturn));
#endif

  struct PhysGuard {
    static constexpr u32 ARMED = 0x6A52D00Du;   // anything else = unarmed
    PhysJB mJB;
    uv32 mArmed;
    sv32 mCode;

    void arm() { mArmed = ARMED; }
    void disarm() { mArmed = 0u; }
    bool isArmed() const { return mArmed == ARMED; }

    /* returns only by jumping to the guard; unarmed = FAIL(code) */
    __attribute__((noreturn)) void jumpOrDie(s32 code) {
      if (mArmed == ARMED) {
        mCode = code;
        mArmed = 0u;
        PHYSGUARD_LONGJMP(mJB, 1);
      }
#ifdef PHYSGUARD_HOSTTEST
      mCode = code;
      __builtin_trap();
#else
      FATAL(code);
#endif
    }
  };
}

#define PHYSICS_FAIL(code) mGuard.jumpOrDie(FAILCode::code)
