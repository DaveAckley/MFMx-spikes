#pragma once    /* -*- C++ -*- */

#include "EwpBlock.h" // for EwpPayload
#include "RecoverPoint.h" // for RecoverPoint
#include "BytePhys.h"     // the bytecode spike's machine
#include "ByteCache.h"    // its programs, cached in fast RAM

namespace MFM {
  struct Physics {
    EventWindow mFastEW;

    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
    u32 mEWsPhysFailed;         // FAILed in physics, recovered (subset of Failed)
    u32 mEWsSincePew;           // attempts since the last pew* marks (no %)

    RecoverPoint mRecover;      // FAIL during physics jumps here (this hart's
                                // fAll.mRecover points at it while physics
                                // runs); lives here, not on the stack

    // Bytecode spike (BytePhys.h, types PHY_BFB1/PHY_BFB4): the machine's
    // operand and call stacks live here in fast RAM, beside mFastEW.
    BytePhys::Machine mVM;

    // The code chunks in use, copied into fast RAM (ByteCache.h): opcode
    // fetches cost 2 cycles there against 8+ in L1.  12 slots of 256
    // bytes, ~3 KB: Liv's hart B budget is ~2 KB stack reserve + ~3 KB
    // cache (BYTEPHYS.md).  Counts go out as `.bc` every 100,000 events,
    // with the stack high-water `.sw`.
    enum : u32 {
      CODE_CACHE_SLOTS = 12,
    };
    BytePhys::CodeCache<CODE_CACHE_SLOTS> mCodeCache;

    // Physics timing (`.pt` mark): per timed type, events, wall-clock
    // ticks (AI clock cycles) spent in guardedPhysics, and bytecode
    // instructions, since the last mark.
    enum : u32 {
      PT_SLOTS = 4,             //< FB1, FB4, BFB1, BFB4
    };
    u32 mPtEvents[PT_SLOTS];
    u32 mPtTicks[PT_SLOTS];
    u32 mPtInsns[PT_SLOTS];
    void notePhysicsTime(u32 type, u32 ticks) ;

    enum GuardedResult : u32 { GR_FALSE = 0, GR_TRUE = 1, GR_JUMPED = 2 };
    GuardedResult guardedPhysics() ;

    bool doTransition(EwpPayload & ewt) ;

    void ewL1ToFast(EventWindow & ewl) {
      memcpy((void*) &mFastEW, (const void*) &ewl, sizeof(EventWindow));
    }
    void ewFastToL1(EventWindow & ewf) {
      memcpy((void*) &ewf, (const void*) &mFastEW, sizeof(EventWindow));
    }

    bool doFastPhysics() ;
    void noteCacheAndStack() ;  // `.bc` and `.sw` marks
    bool runBench() ;           // PHY_BENCH: one microbenchmark program
  };
};
