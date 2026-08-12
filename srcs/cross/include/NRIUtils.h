#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "UxC.h"
#include "S8C.h"
//#include "Wrap8.h"
#include "DefinedConstants.h" // for T6_IMAGE_BLOCK_ADDR
#include "ExtraConstants.h"
#include "Printf.h"
#include "EventWindow.h"
//#include "CornerState.h"
#include "ImageBlock.h"
#include "BlockCode.h"
#include "CrossUtils.h"
#include "TCState.h"

namespace MFM {

  // We have P150B boards, so we are using the PCIe 0 tile, which is
  // at (2,0) in NoC0 coords
  static constexpr U8C PCIeTILE_NOC0 = {2,0};

  /** NoC byte offsets (arg to getNIUAddress) */
  //static constexpr u32 NOC_NODE_ID = 0x44;

  /** NIU Request Initiator byte offsets (1st arg to getNIUReqAddress) */

  static constexpr u32 NRI_NOC_TARG_ADDR_LO = 0x00;
  static constexpr u32 NRI_NOC_TARG_ADDR_MID = 0x04;
  static constexpr u32 NRI_NOC_TARG_ADDR_HI = 0x08;

  static constexpr u32 NRI_NOC_RET_ADDR_LO = 0x0c;
  static constexpr u32 NRI_NOC_RET_ADDR_MID = 0x10;
  static constexpr u32 NRI_NOC_RET_ADDR_HI = 0x14;

  static constexpr u32 NRI_NOC_PACKET_TAG = 0x18;

  static constexpr u32 NRI_NOC_CTRL = 0x1c;
  static constexpr u32 NRI_NOC_AT_LEN_BE = 0x20;
  static constexpr u32 NRI_NOC_AT_LEN_BE_1 = 0x24;

  static constexpr u32 NRI_NOC_AT_DATA = 0x28;

  static constexpr u32 NRI_NOC_CMD_CTRL = 0x40;

  inline u8 stepsN(U8C s0, U8C d0) { return (s0.y >= d0.y) ? s0.y - d0.y : 12u - d0.y + s0.y; }
  inline u8 stepsW(U8C s0, U8C d0) { return (s0.x >= d0.x) ? s0.x - d0.x : 16u - d0.y + s0.y; }
  inline u8 stepsS(U8C s0, U8C d0) { return (s0.y <= d0.y) ? d0.y - s0.y : 12u - s0.y + d0.y; }
  inline u8 stepsE(U8C s0, U8C d0) { return (s0.x <= d0.x) ? d0.x - s0.x : 12u - s0.x + d0.x; }

  u32 preferNoC(U8C sourceNoC0, U8C destNoC0) ; // return 0 or 1 for noc with least hops s->d

  inline u32 funcGetNIUBaseAddress(u32 noc) { return noc == 0u ? NIU_BASE_NOC0 : NIU_BASE_NOC1; }
  inline volatile u32 * funcGetNIUAddress(u32 noc, u32 byteoffset) {
    return (volatile u32*) (funcGetNIUBaseAddress(noc) + byteoffset);
  }
  inline u32 funcReadNIUAddress(u32 noc, u32 byteoffset) {
    return *funcGetNIUAddress(noc,byteoffset);
  }

  inline void funcWriteNIUAddress(u32 noc, u32 byteoffset, u32 value) {
    volatile u32 * p = funcGetNIUAddress(noc,byteoffset);
    *p = value;
  }

  inline u32 funcGetNRIBaseAddress(u32 noc, u32 nri) {
    return funcGetNIUBaseAddress(noc) + nri * NOC_CMD_BUF_OFFSET;
  }

  inline u8 readNIUReqsOutstanding(u32 noc, u32 transactionid) {
    u32 tidoffset =
      0x200 +                   // byte offset to counter space
      (16 + transactionid) *    // plus offset in words..
      4u;                       // ..converted to bytes
    return (u8) funcReadNIUAddress(noc,tidoffset);
  }

  inline volatile u32 * funcGetNRIAddress(u32 noc, u32 nri, u32 byteOffset) {
    return (volatile u32*) (funcGetNRIBaseAddress(noc, nri) + byteOffset);
  }

  inline u32 funcReadNRIAddress(u32 noc, u32 nri, u32 byteOffset) {
    return *funcGetNRIAddress(noc,nri,byteOffset);
  }

  extern void funcWriteNRIAddress(u32 noc, u32 nri, u32 byteOffset, u32 value) ;

  namespace NRI3 {

    constexpr u8 NRI3_BLOCKING_TRANSACTION_ID = 0xe;

    /** Initiate a packet write to the host. sourcedata is the source
        L1 address. Destaddr is a u64 host address formatted for PCIe
        traversal, and note WE ARE NOT DEALING WITH THE PCIe
        TRANSACTION ATTRIBUTES stuff in the top six bits of destaddr!
        Whatever the caller provides we just pass on. */
    s32 initiateWriteToHost(U8C sourcenoc0, u32 * sourcedata, u32 wordCount, u64 destaddr) ;

    /** Initiate an inter-T6 packet write. sourcedata and destaddr
        should both be L1 addresses, and they at least need to be
        equal to each other mod 64, and should probably both just be
        equal to 0 mod 16. */
    s32 initiateWriteToT6(U8C sourcenoc0, u32 * sourcedata, u32 wordCount, U8C destnoc0, u32 destaddr) ;
    
    /** Read up to 16 words from ngb's L1. Note that destaddr MAY
        point to fast local memory, because the NoC read result is
        copied there from a private internal L1 buffer anyway.
     */
    bool blockingL1ReadCT6(U8C ct6us, U8C ct6readfrom, u32 l1readaddr, u32 wordcount, u32 * destaddr, bool debug = false) ; 
    bool blockingL1ReadNoC0(U8C usnoc0, U8C fromnoc0, u32 l1readaddr, u32 wordcount, u32 * destaddr, bool debug = false) ; 

    bool findBlockCodeInNoC0(U8C ournoc0, U8C theirnoc0, BlockCode bc, ImageBlockAddr & foundiba) ;

    ImageBlockHeader blockingReadImageBlockHeaderNoC0(U8C ournoc0, U8C fromNoC0) ;
    ImageBlockAddr blockingReadImageBlockAddrNoC0(U8C ournoc0, U8C fromNoC0, u32 ibaIndex) ;

    ImageBlockHeader blockingReadImageBlockHeaderCT6Offset(U8C ournoc0, S8C ct6off, bool debug = false) ;
    ImageBlockAddr blockingReadImageBlockAddrCT6Offset(U8C ournoc0, S8C ct6off, u32 ibaIndex, bool debug = false) ;

    //// NRI LEVEL
    inline u32 readNRIAddress(u32 noc, u32 byteOffset) { return funcReadNRIAddress(noc,3,byteOffset); }
    inline void writeNRIAddress(u32 noc, u32 byteOffset, u32 value) {
      funcWriteNRIAddress(noc, 3, byteOffset, value);
    }

    inline bool isNRIBusy(u8 noc) { return funcReadNRIAddress(noc,3,NRI_NOC_CMD_CTRL) & 1; }
    void waitTilNRIClear(u8 noc) ;

  };
}
