#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

  struct InterHubPayload {
    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        return false;
      }
      if (inside) {
        ++mOrigin.x;
      } else {
        ++mOrigin.y;
      }
      return true;
    }

    bool isValid() const {
      return mRange.area() < MAX_ATOMS;
    }

    S16C mOrigin;               //< origin relative coord, updates on transits
    U16CRange mRange;
    static constexpr u32 MAX_ATOMS = ((1u<<12) - sizeof(mOrigin) - sizeof(mRange)) / sizeof(P4Atom);
    P4Atom mAtoms[MAX_ATOMS];
  };
  
  class InterHubBlock : public TC<InterHubBlock,sizeof(InterHubPayload)> {
  public:
    const char * getName() const { return "InterHubBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    InterHubPayload & payload() { return *(InterHubPayload*) getDataStart(); }
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
  static_assert(sizeof(InterHubBlock)==4160,"InterHubBlock size check failed");

  typedef TCStorage<InterHubBlock,5> InterHubStorage;
  static_assert(sizeof(InterHubStorage)==20800,"InterHubStorage size check failed");
}
