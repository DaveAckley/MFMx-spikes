#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "U16C.h"
#include "ExtraConstants.h"
#include "HostBlock.h"

namespace MFM {
  static const volatile u32 * ET_NIU_BASE = NIU_ADDRESS(0x0,0,0);
  static const volatile u32 * ET_NIU_NODE_ID = NIU_ADDRESS(0x44,0,0);
  static const volatile u32 * ET_NIU_ENDPOINT_ID = NIU_ADDRESS(0x48,0,0);

  struct T6ElevatorTransport {
    T6ElevatorTransport(HostBlock & hb)
      : mHostBlock(hb)
    { }
    bool allClear() ;
    s32 initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) ;

    HostBlock & mHostBlock;
  };
}


