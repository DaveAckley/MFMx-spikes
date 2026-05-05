#pragma once    /* -*- C++ -*- */

#include "EwpBlock.h" // for EwpPayload

namespace MFM {
  struct Physics {
    EventWindow mFastEW;

    u32 mEWsAttempted;
    u32 mEWsSucceeded;
    u32 mEWsFailed;

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
