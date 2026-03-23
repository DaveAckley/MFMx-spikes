/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"
#include "TCCommon.h"

namespace MFM {

  // (CRTP) Base for Transportable Content functions
  template<class SUBTC, class PAYLOAD>
  struct alignas(16) TC : public TCCommon {

    // self(): access this by subtype
    SUBTC& self() { return static_cast<SUBTC&>(*this); }
    SUBTC const & self() const { return static_cast<SUBTC const&>(*this); }

    PAYLOAD & payload() { return mPayload; }
    PAYLOAD const & payload() const { return mPayload; }

    // API
    bool readyToClose(TCOpsData & tms, u32 msnow) const { return self().readyToCloseTC(tms,msnow); }
    void reset() { self().resetTC(); }

    // SERVICES
    bool isEmpty() { return getHeader().mTCSType == TCType::EMPTY; }

    TCSig getHeader() const { return mHeader; }
    TCSig & getHeader() { return mHeader; }

    TCSig getFooter() const {
      u8 t = mHeader.mTCSType;
      if (t == TCType::STANDARD) return mFooter;
      if (t == TCType::EMPTY) {
        const TCSig * pheader = &mHeader;
        return pheader[1];
      }
      return TCSig();
    }
    TCSig & getFooter() {
      u8 t = mHeader.mTCSType;
      if (t == TCType::STANDARD) return mFooter;
      if (t == TCType::EMPTY) {
        TCSig * pheader = &mHeader;
        return pheader[1];
      }
      FAIL(ILLEGAL_STATE); // got no footer?
    }

    bool isComplete() const {
      TCSig h = getHeader();
      if (!h.isValid()) return false;
      TCSig f = getFooter();
      return h == f;
    }
    
    TCState getTCState() const { return getHeader().getTCState(); }
    void setTCState(TCState state, TCType type) {
      TCSig & h = getHeader();
      h.setTCState(state,type);
      if (h.mTCSType == TCType::STANDARD) mFooter = mHeader;
      else if (h.mTCSType == TCType::EMPTY) {
        TCSig * pheader = &mHeader;
        pheader[1] = mHeader;   // stomp footer right after header if no payload
      } else FAIL(ILLEGAL_STATE);
    }

  protected:
    TC() = default; // don't make these

    TCSig mHeader;
    PAYLOAD mPayload;
    TCSig mFooter;
  };

}
