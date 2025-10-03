#pragma once                  // -*- C++ -*-
#include "itype.h"

namespace MFM {
  static const u32 NIU_BASE_NOC0 =    0xFFB20000;
  static const u32 NIU_BASE_NOC1 =    0xFFB30000;

  static const u32 NOC_CMD_BUF_OFFSET =   0x0800; // distance between NIU request initiators

#define NIU_ADDRESS(offset,reqinit,noc) \
  ((volatile u32 *)                                                     \
  ((noc == 0u ? NIU_BASE_NOC0 : NIU_BASE_NOC1) + reqinit * NOC_CMD_BUF_OFFSET + offset) \
   )

  static const volatile u32 * NOC_NODE_ID0 =     NIU_ADDRESS(0x44,0,0);
  static const volatile u32 * NOC_NODE_ID1 =     NIU_ADDRESS(0x44,0,1);
  static const volatile u32 * NOC_ENDPOINT_ID0 = NIU_ADDRESS(0x48,0,0);
  static const volatile u32 * NOC_ENDPOINT_ID1 = NIU_ADDRESS(0x48,0,1);
}
