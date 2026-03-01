#include "NRIUtils.h"
#include "Fail.h"
#include "CrossUtils.h"
#include "Printf.h"
#include "FastLocal.h" // for FAll
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for create
#include "T6Grid.h"


#define LOGP

#ifdef LOGP
#define P LOG
#else
#define P DP
#endif

namespace MFM {

  /** T6-wide NoC x NRI Assignments

            NoC 0                 NoC 1
        +-------------------+ +-------------------+
    NRI0|PCIe comm on       | | unassigned        |
        |T6Elevator         | |[Future PCIe       |
        |Platform           | | comm ?]           |
        +-------------------+ +-------------------+
    NRI1|T6RingO SE Corner  | |T6RingO NW Corner  |
        |@NOC0_ROUTER_CFG_2 | |@NOC1_ROUTER_CFG_2 |
        |    0xffb2'010C    | |    0xffb3'010C    |
        |writes EAST to SW  | |writes WEST to NE  |
        |(NOC0_ROUTER_CFG_4)| |(NOC1_ROUTER_CFG_4)|
        |    0xffb2'0114    | |    0xffb3'0114    |
        +-------------------+ +-------------------+
    NRI2|T6RingO SW Corner  | |T6RingO NE Corner  |
        |@NOC0_ROUTER_CFG_4 | |@NOC1_ROUTER_CFG_4 |
        |    0xffb2'0114    | |    0xffb3'0114    |
        |writes SOUTH to NW | |writes NORTH to SE |
        |(NOC1_ROUTER_CFG_2)| |(NOC0_ROUTER_CFG_2)|
        |    0xffb3'010C    | |    0xffb2'010C    |
        +-------------------+ +-------------------+
    NRI3| see struct NRI3   | | see struct NRI3   |
        +-------------------+ +-------------------+
   */

  static constexpr u32 NOC0_ROUTER_CFG_2 = 0xffb2'010C; //< stg for state incoming to SE
  static constexpr u32 NOC0_ROUTER_CFG_4 = 0xffb2'0114; //< stg for state incoming to SW
  static constexpr u32 NOC1_ROUTER_CFG_2 = 0xffb3'010C; //< stg for state incoming to NW
  static constexpr u32 NOC1_ROUTER_CFG_4 = 0xffb3'0114; //< stg for state incoming to NE

  static constexpr u32 INBOUND_TO_SE = NOC0_ROUTER_CFG_2;
  static constexpr u32 INBOUND_TO_SW = NOC0_ROUTER_CFG_4;
  static constexpr u32 INBOUND_TO_NW = NOC1_ROUTER_CFG_2;
  static constexpr u32 INBOUND_TO_NE = NOC1_ROUTER_CFG_4;

  void funcWriteNRIAddress(u32 noc, u32 nri, u32 byteOffset, u32 value) {
    volatile u32 * p = funcGetNRIAddress(noc, nri, byteOffset);
    if (false)
      P.printf("FWNA 0x%08x=%d/0x%x\n", (u32) p, value, value);
    *p = value;
  }

  bool NRI3::blockingL1Read(U8C ct6us, U8C ct6readfrom, u32 l1readaddr,
                            u32 wordcount, u32 * destaddr) { 
    u32 useNoC = 0u;
    const u32 MAXWORDS = 16u;
    if (wordcount > MAXWORDS) return false;
    
    volatile static u8 readBuffer[MAXWORDS*4u+16u+4u]; // In L1 For Sure
    u32 baseaddr = (u32) &readBuffer[0];
    {
      /* ASSUMING THE WORMHOLEB0 RESTRICTIONS APPLY TO BLACKHOLEA0, SINCE
         https://github.com/tenstorrent/tt-isa-documentation/blob/main/BlackholeA0/NoC/Alignment.md
         IS A FUCKING 404 ON Wed Jan 28 11:08:17 2026 
         AND IS STILL 404 ON Sun Feb 22 15:36:47 2026 
      */

      u32 sm16 = l1readaddr % 16;
      u32 dm16 = baseaddr % 16;
      u32 adj = sm16 - dm16; // might underflow?
      if (adj != 0u)
        baseaddr += 16u + adj;
      u32 fm16 = baseaddr % 16;
      P.printf("BL1RS RVL1 s%u - d%u = %u, f%d [0x%08x] @ 0x%08x for 0x%x\n",
               sm16, dm16, adj, fm16,
               readBuffer,baseaddr,l1readaddr);
    }
    u32 spin = 0u;
    while (isNRIBusy(useNoC)) {  
      if ((++spin % 0xfffff) == 0u) {
        P.printf("BL1R enter long block 0x%x\n",spin);
        return false;
      }
    }

    U8C usnocc = U8C::makeNoC0CoordFromCT6Coord(ct6us);
    U8C themnocc = U8C::makeNoC0CoordFromCT6Coord(ct6readfrom);
    if (!U8C::onBoardNoC0Coord(usnocc) ||
        !U8C::onBoardNoC0Coord(themnocc))
      return false;

    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_LO, l1readaddr); // 32 bit address of source
    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(themnocc));

    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_LO, baseaddr); // return read value here
    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(usnocc));

    writeNRIAddress(useNoC,NRI_NOC_PACKET_TAG, NRI3_BLOCKING_TRANSACTION_ID<<10); // set transaction ID
    writeNRIAddress(useNoC,NRI_NOC_CTRL, (0u<<0));          // NOC_CMD_RD

    //    writeNRIAddress(NRI_NOC_AT_LEN_BE, 1u<<2u);      // bytecount to read
    writeNRIAddress(useNoC,NRI_NOC_AT_LEN_BE, wordcount<<2u);      // bytecount to read
    writeNRIAddress(useNoC,NRI_NOC_AT_LEN_BE_1, 0);         // not dealing with masks or etc

    writeNRIAddress(useNoC,NRI_NOC_CMD_CTRL,1);             // initiate write
    readNRIAddress(useNoC,NRI_NOC_CMD_CTRL);                // read for memory ordering

    XXX_DEBUG_FUNC(__FILE__,__LINE__);

    spin = 0u;
    while (readNIUReqsOutstanding(useNoC, NRI3_BLOCKING_TRANSACTION_ID) > 0) {
      if ((++spin % 0xfffff) == 0u) {
        P.printf("BL1R xaction long block 0x%x\n",spin);
        return false;
      }
    }

    XXX_DEBUG_FUNC(__FILE__,__LINE__);

    u32 *data = (u32*) baseaddr;
    P.printf("BL1RX RVL1 TO 0x%08x READ %uB (after %d)\n",
             baseaddr, wordcount<<2u, spin);
    for (u32 i = 0u; i < wordcount; ++i) {
      destaddr[i] = data[i];
      P.printf("BL1RY %d:0x%08x\n", i, destaddr[i]);
    }
    return true;
  }

  ImageBlockHeader NRI3::blockingReadImageBlockHeader(U8C usnoc, S8C ct6off) {
    ImageBlockHeader ret;       // uninit -> INVALID
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    if (!U8C::onBoardCT6Coord(usct6)) return ret;

    U8C themct6 = usct6 + ct6off;
    if (!U8C::onBoardCT6Coord(themct6)) return ret;

    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'

    blockingL1Read(usct6, themct6,
                   (u32) ibux14,
                   sizeof(ImageBlockHeader)>>2u,
                   (u32*) &ret);
    return ret; //< whether read succeeded (then us too) or not (then us neither)
  }

  ImageBlockAddr NRI3::blockingReadImageBlockAddr(U8C usnoc, S8C ct6off, u32 ibaIndex) {
    ImageBlockAddr ret;       // uninit -> INVALID
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    if (!U8C::onBoardCT6Coord(usct6)) return ret;

    U8C themct6 = usct6 + ct6off;
    if (!U8C::onBoardCT6Coord(themct6)) return ret;

    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'
    const u32 *iba = ibux14 + 1u + ibaIndex*(sizeof(ImageBlockAddr)>>2u);

    blockingL1Read(usct6, themct6,
                   (u32) iba,
                   sizeof(ImageBlockAddr)>>2u,
                   (u32*) &ret);
    return ret; //< whether read succeeded (then us too) or not (then us neither)
  }

}

