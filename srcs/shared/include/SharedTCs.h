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
      HBNOTE(inside ? "IHBI" : "IHBO");
      if (inside) {
        ++mOrigin.x;
        HBPVAL(mOrigin);
      } else {
        ++mOrigin.y;
        HBPVAL(mOrigin);
      }
      return true;
    }

    S16C mOrigin;               //< origin relative coord, updates on transits
    u16 mAtomInfoCount;
    struct AtomInfo {
      S16C mOffset;             //< position relative to mOrigin
      P4Atom mAtom;
    };
    static constexpr u32 MAX_ATOMS = ((1u<<12) - sizeof(mOrigin)) / sizeof(AtomInfo);
    AtomInfo mAtomInfos[MAX_ATOMS];
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
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
      {
        TCWord twctxx;
        TCMarker h = getHeader();
        HBPTAG(-?-,h.isValid());
        twctxx.mMarker = h;
        HBXTAG(huh,twctxx.mWord);
        HBXTAG(hmg,(u32)h.mTCMMagic);
        HBXTAG(hnc,(u32)h.mTCMNonce);
        HBXTAG(hsz,(u32)h.mTCMSize);
        HBXTAG(hst,(u32)h.mTCMState);
        HBXTAG(*hh,&getHeader());
        HBPTAG(tcm,h.mTCMSize);
        HBXTAG(*ff,&getFooter());
        twctxx.mMarker = getFooter();
        HBXTAG(2ff,twctxx.mWord);
        HBXTAG(this,this);
        HBXTAG(*ak,&getAnkleOrDie());
        twctxx.mMarker = getAnkleOrDie();
        HBXTAG(2aa,twctxx.mWord);
        HBPTAG(-f-,getFooter().isValid());
        HBPTAG(-a-,getAnkle().isValid());
        HBPTAG(-==,getFooter() == getHeader());
        HBPTAG(-s-,h.mTCMSize);
        HBPTAG(-O-,isComplete());
      }
    }
  };

  typedef TCStorage<InterHubBlock,2> InterHubStorage;

  struct EwpPayload {
    EventWindow mOld, mNew;
    s32 mHiddenXPos, mHiddenYPos; // host side use only
    //TimeStamp mSTVLTime;          // host side use only
    void init() {
      mHiddenXPos = 0;
      mHiddenYPos = 0;
    }
    bool update(bool inside) { 
      HBNOTE(inside ? "EUPI" : "EUPO");
      if (inside) {
        ++mHiddenXPos;
        HBPVAL(mHiddenXPos);
      } else {
        ++mHiddenYPos;
        HBPVAL(mHiddenYPos);
      }
      return true;
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
