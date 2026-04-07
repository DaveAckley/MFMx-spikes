#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"

namespace MFM {
  
  struct EwpPayload {
    EventWindow mOld, mNew;
    s32 mHiddenXPos, mHiddenYPos; // host side use only
    //TimeStamp mSTVLTime;          // host side use only
    void init() { }
    bool update(bool inside) { return true; }
  };

  class EwpBlock : public TC<EwpBlock,sizeof(EwpPayload)> {
  public:
    const char * getName() const { return "EwpBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    EwpPayload & payload() { return *(EwpPayload*) getDataStart(); }
    void init() {
      TC::reset();
      setTCState(TCState::UNUSED,sizeof(EwpPayload));
      payload().init();
    }
  };

}
