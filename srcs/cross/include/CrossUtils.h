#pragma once /* -*- C++ -*- */

#include "itype.h"
#include <string.h>

namespace MFM {
  inline void memset_s(void* addr, u8 byte, u32 count) {
    memset(addr,byte,count); // don't have explicit_bzero in libs I'm using?
  }
}
