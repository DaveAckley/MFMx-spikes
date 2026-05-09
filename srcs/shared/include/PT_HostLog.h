#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

  struct HostLogPayload {
    static constexpr u32 HLP_MAGIC = 0x11a2b;
    u16 mMAGIC;
    u16 mPayloadLength;
    u32 mTicksBase;
    static constexpr u32 HLP_MAX_LEN = sizeof(HostLogPayload) - offsetof(HostLogPayload,mTicksBase);
    u8 mData[HLP_MAX_LEN];

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    void reset() {
      mMAGIC = HLP_MAGIC;
    }

    bool update(bool inside) { 
      if (!isValid()) {
        return false;
      }
      FAIL(INCOMPLETE_CODE);
      return true;
    }

    bool isValid() const {
      return
        mMAGIC == HLP_MAGIC &&
        mPayloadLength <= HLP_MAX_LEN;
    }

    static constexpr u32 MAX_ATOMS = ((1u<<12) - sizeof(mOrigin)) / sizeof(AtomInfo);
    AtomInfo mAtomInfos[MAX_ATOMS];
    u32 mCIGAM;
  };
  
  class HostLogBlock : public TC<HostLogBlock,sizeof(HostLogPayload)> {
  public:
    const char * getName() const { return "HostLogBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    HostLogPayload & payload() { return *(HostLogPayload*) getDataStart(); }
    void init() {
      //HBNOTE(getName());
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init();
      //HBNOTE(preCloseTC);
      //      payload().checksCheck();
      closeTC(sizeof(payload())); // and then close it, with a full load
      //HBNOTE(postCloseTC);
      //      payload().checksCheck();
      //HBNOTE(postSetDepTC);
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
      //      payload().checksCheck();
    }
  };

  typedef TCStorage<HostLogBlock,2> HostLogStorage;
}
