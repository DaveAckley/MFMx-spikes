#include "T6ElevatorTransport.h"

namespace MFM {
  //// ALL DEFS HERE ARE REQUEST INITIATOR #0 ON NOC 0!
  static const volatile u32 * NOC_TARG_ADDR_LO =  NIU_ADDRESS(0x00,0,0);
  static const volatile u32 * NOC_TARG_ADDR_MID = NIU_ADDRESS(0x04,0,0);
  static const volatile u32 * NOC_TARG_ADDR_HI =  NIU_ADDRESS(0x08,0,0);

  static const volatile u32 * NOC_RET_ADDR_LO =   NIU_ADDRESS(0x0C,0,0);
  static const volatile u32 * NOC_RET_ADDR_MID =  NIU_ADDRESS(0x10,0,0);
  static const volatile u32 * NOC_RET_ADDR_HI =   NIU_ADDRESS(0x14,0,0);

  static const volatile u32 * NOC_CTRL =          NIU_ADDRESS(0x1C,0,0);
  static const volatile u32 * NOC_AT_LEN_BE =     NIU_ADDRESS(0x20,0,0);

  static const volatile u32 * NOC_AT_DATA =       NIU_ADDRESS(0x28,0,0);

  static const volatile u32 * NOC_CMD_CTRL =      NIU_ADDRESS(0x40,0,0);

  bool T6ElevatorTransport::allClear() {
    u32 v = *NOC_CMD_CTRL;
    return 0u==(v&1);
  }

  s32 T6ElevatorTransport::initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) {
    if (!allClear()) return -1; // Not ready
    u32 byteCount = count * 4;
    if (byteCount == 0 || byteCount > (1<<14))
      return -2;                // EINVAL: bad size

    u32 targlo = (u32) data;
    u32 targmid = 0u;
    u32 targhi = ((mHostBlock.mYPos&0x3f)<<6)|(mHostBlock.mXPos&0x3f);

    u32 retlo = (u32) (destaddr&0xffffffff);
    u32 retmid = (u32) ((destaddr>>32)&0xffffffff);
    u32 rethi = ((xy.y&0x3f)<<6)|(xy.x&0x3f);
    u32 noc_ctrl = (2<<0);      // write request

    return 0;
  }

}
