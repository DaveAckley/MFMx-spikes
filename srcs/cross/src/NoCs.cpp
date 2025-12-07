#include "NoCs.h"
#include "S8C.h"

namespace MFM {

  s32 NoC::initiateWrite(u32 * data, u32 wordCount, U8C destxy, u64 destaddr, u32 reqinit) const {
    if (!reqAllClear(reqinit)) return -1; // Not ready

    u32 byteCount = wordCount * 4;
    if (byteCount == 0 || byteCount > (1<<14))
      return -2;                // EINVAL: bad size

    // TARG (lo+mid) is the source in our L1. TARG hi is our raw NoC coord
    u32 targlo = (u32) data;
    u32 targmid = 0u;
    u32 targhi = U8C::makeNoCNodeIdFromU8CCoord(mNoCXY);

    // In general:
    //   RET (lo+mid) is the dest addr, RET hi is the raw NoC coord of the dest tile
    // For initiateWriteToHost specifically:
    //   RET (lo+mid) is the dest in Host RAM, RET hi is the raw NoC coord of the PCIe tile ?
    u32 retlo = (u32) (destaddr&0xffffffff);
    u32 retmid = (u32) ((destaddr>>32)&0xffffffff);
    u32 rethi = U8C::makeNoCNodeIdFromU8CCoord(destxy);
    u32 noc_ctrl = (2<<0);      // write request

    // Set up the registers
    writeNIUReqAddress(NRI_NOC_TARG_ADDR_LO, reqinit, targlo);
    writeNIUReqAddress(NRI_NOC_TARG_ADDR_MID, reqinit, targmid);
    writeNIUReqAddress(NRI_NOC_TARG_ADDR_HI, reqinit, targhi);

    writeNIUReqAddress(NRI_NOC_RET_ADDR_LO, reqinit, retlo);
    writeNIUReqAddress(NRI_NOC_RET_ADDR_MID, reqinit, retmid);
    writeNIUReqAddress(NRI_NOC_RET_ADDR_HI, reqinit, rethi);

    writeNIUReqAddress(NRI_NOC_CTRL, reqinit, 2);
    writeNIUReqAddress(NRI_NOC_AT_LEN_BE, reqinit, byteCount);

    // We are ready to initiate the NoC transaction?
    writeNIUReqAddress(NRI_NOC_CMD_CTRL, reqinit, 1);            // THE BIRD IS AWAY
    u32 readback = readNIUReqAddress(NRI_NOC_CMD_CTRL, reqinit);  // read it back for memory ordering?
    return 0;
   }

  bool NoCs::makeNgbNoCRequest(Dir8 ngbdir, u32 reqinit, NoCRequest & req) {
    U8C ouraddr0 = mNoC0.mNoCXY; // get addr in NoC0 terms
    S8C off = S8C::makeS8CFromDir8(ngbdir); // get offset in NoC0 terms
    U8C theiraddr0 = ouraddr0;
    if (!theiraddr0.addTo(off)) // there ain't no
      return false;             // there there
    // PRIMITIVE MOVES
    // TO GO, USE NoC, TO NoC DELTA
    //   N      1      (0,1)
    //   E      0      (1,0)
    //   S      0      (0,1)
    //   W      1      (1,0)
    //   SE     0      (1,1)
    //   NW     1      (1,1)
    // COMPOUND MOVES
    //   NE     N,E   
    //   SW     S,W
    

    FAIL(INCOMPLETE_CODE);
  }

}
