#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

  enum EwpPayloadCode : u8 {
    EWPC_UNINITTED = 0u,
    EWPC_EMPTY,
    EWPC_SOURCE_ONLY,
    EWPC_SOURCE_AND_DEST
  };

  struct EwpPayloadState {
    EwpPayloadCode mPayloadCode;
  };
  struct EwpPayload {
    EwpPayloadState mPayloadState;
    s32 mHiddenXPos, mHiddenYPos; // host side use only
    EventWindow mOld, mNew;
    //TimeStamp mSTVLTime;          // host side use only
    void init() {
      mHiddenXPos = 0;
      mHiddenYPos = 0;
    }

    static constexpr u32 payloadSizeFromCode(EwpPayloadCode plc) {
      switch(plc) {
      case EwpPayloadCode::EWPC_UNINITTED:
      case EwpPayloadCode::EWPC_EMPTY:
        return offsetof(EwpPayload,mHiddenXPos);

      case EwpPayloadCode::EWPC_SOURCE_ONLY:
        return offsetof(EwpPayload,mNew);

      case EwpPayloadCode::EWPC_SOURCE_AND_DEST:
        return sizeof(EwpPayload);
      }
      return 0u;                // 'unreachable'
    }

    u32 currentPayloadSize() const {
      return payloadSizeFromCode(mPayloadState.mPayloadCode);
    }
  };

  class EwpBlock : public TC<EwpBlock,sizeof(EwpPayload)> {
  public:
    const char * getName() const { return "EwpBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    EwpPayload & payload() { return *(EwpPayload*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };

}
