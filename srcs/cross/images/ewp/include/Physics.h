#pragma once    /* -*- C++ -*- */

#include "EwpBlock.h" // for EwpPayload
#include "RecoverPoint.h" // for RecoverPoint

namespace MFM {
  struct Physics {
    EventWindow mFastEW;

    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;
    u32 mEWsPhysFailed;         // FAILed in physics, recovered (subset of Failed)

    RecoverPoint mRecover;      // FAIL during physics jumps here (this hart's
                                // fAll.mRecover points at it while physics
                                // runs); lives here, not on the stack

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
  };
};
