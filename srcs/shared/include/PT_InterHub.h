#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

  struct InterHubPayload {
    static constexpr u32 IHP_MAGIC = 0x4948504d;
    static constexpr u32 IHP_CIGAM = 0x49485043;
    void init() {
      memset_s(this,'\0',sizeof(*this));
      mMAGIC = IHP_MAGIC;
      mCIGAM = IHP_CIGAM;
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        HBPTAG(ihpma,mMAGIC);
        HBPTAG(ihpci,mCIGAM);
        return false;
      }
      //      HBPTAG(IHAT,this);
      //      HBPTAG(ISIZ,sizeof(*this));
      //      HBXTAG(IEND,(u32)(((char*)this)+sizeof(*this)));
      ++mAtomInfoCount;
      //      HBPTAG(mAIC,mAtomInfoCount);
      if (inside) {
        ++mOrigin.x;
        //        HBPTAG(IHP-i,(u32) mOrigin.x);
      } else {
        ++mOrigin.y;
        //        HBPTAG(IHP-o,(u32) mOrigin.y);
      }
      //      HBPTAG(IHP-a,mOrigin);
      return true;
    }

    bool isValid() const {
      return
        mMAGIC == IHP_MAGIC &&
        mCIGAM == IHP_CIGAM;
    }
    u32 mMAGIC;
    S16C mOrigin;               //< origin relative coord, updates on transits
    u16 mAtomInfoCount;
    struct AtomInfo {
      S16C mOffset;             //< position relative to mOrigin
      P4Atom mAtom;
    };
    static constexpr u32 MAX_ATOMS = ((1u<<12) - sizeof(mOrigin)) / sizeof(AtomInfo);
    AtomInfo mAtomInfos[MAX_ATOMS];
    u32 mCIGAM;
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

  typedef TCStorage<InterHubBlock,2> InterHubStorage;
}
