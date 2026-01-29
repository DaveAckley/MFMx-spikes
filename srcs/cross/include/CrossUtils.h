#pragma once /* -*- C++ -*- */

#include "itype.h"
#include <string.h>

namespace MFM {
  void memset_s(void* addr, u8 byte, u32 count) ;

  void XXX_DEBUG_FUNC(const char * file, u32 line, const char * msg = 0) ;
}
