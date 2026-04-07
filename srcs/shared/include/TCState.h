#pragma once  /* -*- C++ -*- */

namespace MFM {
  enum TCState : u8 {
    UNUSED = 0u,           // 0 under construction (MARKERS INVALID)
    OPEN,                  // 1 available for (un)loading locally (MARKERS INVALID)
    CLOSED,                // 2 finished (un)loading locally (MARKERS VALID)
    INBOUND_DEPARTED,      // 3 left t6/ewp or arrived host/hub (MARKERS VALID)
    OUTBOUND_DEPARTED,     // 4 left host/hub or arrived t6/ewp (MARKERS VALID)
  };
}
