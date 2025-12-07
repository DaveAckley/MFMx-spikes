#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "ExtraConstants.h"
#include "U8C.h"
#include "Fail.h"

namespace MFM {
  /** NoC byte offsets (arg to getNIUAddress) */
  static constexpr u32 NOC_NODE_ID = 0x44;

  /** NIU Request Initiator byte offsets (1st arg to getNIUReqAddress) */

  static constexpr u32 NRI_NOC_TARG_ADDR_LO = 0x00;
  static constexpr u32 NRI_NOC_TARG_ADDR_MID = 0x04;
  static constexpr u32 NRI_NOC_TARG_ADDR_HI = 0x08;

  static constexpr u32 NRI_NOC_RET_ADDR_LO = 0x0c;
  static constexpr u32 NRI_NOC_RET_ADDR_MID = 0x10;
  static constexpr u32 NRI_NOC_RET_ADDR_HI = 0x14;

  static constexpr u32 NRI_NOC_CTRL = 0x1c;
  static constexpr u32 NRI_NOC_AT_LEN_BE = 0x20;

  static constexpr u32 NRI_NOC_CMD_CTRL = 0x40;

  struct NoCRequest {
    U8C mDestXY;                //< raw dest coord in NoC#mNoC terms
    u8 mNoC;                    //< which NoC to use for this request
    u8 mReqInit;                //< and which NIU initiator to use
  };

  struct NoC {
    void init(u8 number, u32 niuBaseAddress) {
      mNIUBaseAddress = niuBaseAddress;
      mNumber = number;
      mNoCXY = U8C::makeU8CFromNoCNodeId(readNIUAddress(NOC_NODE_ID));
    }

    /** get per-NoC NIU MMIO addresses */
    volatile u32 * getNIUAddress(u32 byteoffset) const {
      return (volatile u32 *)
        (mNIUBaseAddress + byteoffset);
    }

    /** get per-NoC, per-NIU request initiator MMIO addrs*/
    volatile u32 * getNIUReqAddress(u32 byteoffset, u32 reqinit) const {
      return (volatile u32 *)
        (mNIUBaseAddress + reqinit * NOC_CMD_BUF_OFFSET + byteoffset);
    }

    void writeNIUReqAddress(u32 byteoffset, u32 reqinit, u32 value) const {
      *getNIUReqAddress(byteoffset, reqinit) = value;
    }

    u32 readNIUAddress(u32 byteoffset) const {
      return *getNIUAddress(byteoffset);
    }

    u32 readNIUReqAddress(u32 byteoffset, u32 reqinit) const {
      return *getNIUReqAddress(byteoffset, reqinit);
    }

    //U8C getXY() const { return mNoCXY; }

    bool reqAllClear(u32 reqinit) const {
      u32 v = readNIUReqAddress(NRI_NOC_CMD_CTRL,reqinit);
      return 0u==(v&1);
    }

    s32 initiateWrite(u32 * data, u32 wordCount, U8C xy, u64 destaddr, u32 reqinit) const ;

    u32 mNIUBaseAddress;
    U8C mNoCXY; //< (x,y) as reckoned in NoC# mNumber
    u8 mNumber;
    u8 mReserved;
  };

  struct NoCs {
    void init() {
      mNoC0.init(0, NIU_BASE_NOC0);
      mNoC1.init(1, NIU_BASE_NOC1);
    }

    NoC & getNoC(u32 idx) {
      if (idx == 0u) return mNoC0;
      if (idx == 1u) return mNoC1;
      FAIL(ILLEGAL_ARGUMENT);
    }

    bool makeNgbNoCRequest(Dir8 ngbdir, u32 reqinit, NoCRequest & req) ;

    NoC mNoC0;
    NoC mNoC1;
  };
}
