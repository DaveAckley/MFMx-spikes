#pragma once       /* -*- C++ -*- */

namespace MFM {

  enum HostFlags : u8 {
    HBF_ASSERT_STOP_LOG = 0x01, //< block further hblogging if any HBASSERT blows
    HBF_NO_UTAG =         0x02, //< exclude unstructured tags from LOG
    HBF_STAG_PTRACE =     0x04, //< include structured PTRACE flags in LOG
    HBF_NO_AUTOSEED =     0x08, //< this chip does not autoseed its grid
    HBF_RSV4 =            0x10,
    HBF_RSV5 =            0x20,
    HBF_RSV6 =            0x40,
    HBF_RSV7 =            0x80
  };
}
