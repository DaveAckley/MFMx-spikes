#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "U16C.h"
#include "ExtraConstants.h"
#include "HostBlock.h"

namespace MFM {
  /*
  static const volatile u32 * ET_NIU_BASE = NIU_ADDRESS(0x0,0,0);
  static const volatile u32 * ET_NIU_NODE_ID = NIU_ADDRESS(0x44,0,0);
  static const volatile u32 * ET_NIU_ENDPOINT_ID = NIU_ADDRESS(0x48,0,0);
  */

  struct T6ElevatorTransport {
    // We have P150B boards, so we are using the PCIe 0 tile, which is
    // at (2,0) in raw NoC0 coords
    static constexpr U16C PCIeTILE_COORD = {2,0};

    T6ElevatorTransport(HostBlock & hb)
      : mHostBlock(hb)
    { }
    bool allClear() ;
    s32 initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) ;
    s32 initiateWriteToHost(u32 * data, u32 count, u64 destaddr) {
      //// NOTE WE ARE NOT DEALING WITH THE PCIe TRANSACTION
      //// ATTRIBUTES STUFF IN THE TOP SIX BITS OF destaddr! WHATEVER
      //// THE CALLER PROVIDES WE JUST GO WITH.
      return initiateWrite(data, count, PCIeTILE_COORD, destaddr);
    }

    HostBlock & mHostBlock;
  };
}


