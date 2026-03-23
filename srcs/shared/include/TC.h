/* -*- C++ -*- */
#pragma once

#include "itype.h"

#include <cstring>
#include "BaseCar.h"
#include "Fail.h"
#include "EventWindow.h"
#include "TimeDefs.h"

//#include "CrossUtils.h"

namespace MFM {

  // (CRTP) Base for Transportable Content functions
  template<class CONTENT>
  struct TC {
    // self(): access this by subtype
    SUB& self() { return static_cast<SUB&>(*this); }
    SUB const & self() const { return static_cast<SUB const&>(*this); }

    bool readyToClose(CarOpsTimers & tms,u32 msnow) const { return self().readyToClose(tms,msnow); }
    bool isEmpty() { return self().isEmpty(); }
    void reset() { self().reset(); }
  protected:
    TC() = default; // don't make these
  };

}
