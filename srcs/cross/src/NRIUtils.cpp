#include "NRIUtils.h"
#include "Fail.h"
#include "Printf.h"
#include "FastLocal.h" // for FAll
#include "FastT0.h" // for millisElapsed
#include "FastT2.h" // for create
#include "T6Grid.h"
#include "Debug.h"
#include "utils.h" // for PopCount

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
    NRI3| see namespace NRI3| | see namespace NRI3|
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

  u32 preferNoC(U8C s0, U8C d0) { // return 0 or 1 for noc with least hops s->d
    u8 dist0 = stepsE(s0,d0) + stepsS(s0,d0);
    u8 dist1 = stepsW(s0,d0) + stepsN(s0,d0);
    if (dist0 < dist1) return 0;
    if (dist0 > dist1) return 1;
    static u32 ties;
    return PopCount(++ties)&1;  // vary choice on ==
  }

  void funcWriteNRIAddress(u32 noc, u32 nri, u32 byteOffset, u32 value) {
    volatile u32 * p = funcGetNRIAddress(noc, nri, byteOffset);
    if (false)
      P.printf("FWNA 0x%08x=%d/0x%x\n", (u32) p, value, value);
    *p = value;
  }

  bool NRI3::blockingL1ReadCT6(U8C ct6us, U8C ct6readfrom, u32 l1readaddr,
                               u32 wordcount, u32 * destaddr, bool debug) { 
    U8C usnocc = U8C::makeNoC0CoordFromCT6Coord(ct6us);
    U8C themnocc = U8C::makeNoC0CoordFromCT6Coord(ct6readfrom);
    //if (debug) HBPVAL(usnocc);
    //if (debug) HBPVAL(themnocc);
    return blockingL1ReadNoC0(usnocc, themnocc, l1readaddr, wordcount, destaddr, debug);
  }

  const u32 PRIVRB_MAXWORDS = 16u;
  volatile u32 privateL1ReadBuffer[PRIVRB_MAXWORDS+4u+1u]; // +4 for alignment +1 for fear

  bool NRI3::blockingL1ReadNoC0(U8C usnoc0, U8C fromnoc0, u32 l1readaddr,
                                u32 wordcount, u32 * destaddr, bool debug) { 
    MFM_API_ASSERT_ON_HART(HARTNUM_NC); // nri3 reserved for hNC
    //if (debug) HBNOTE("BLR0");
    if (!U8C::onBoardNoC0Coord(usnoc0) ||
        !U8C::onBoardNoC0Coord(fromnoc0))
      return false;

    HBASSERT_GT((u32) destaddr, 0x30); // XXX don't deliver to lo mem?
    HBASSERT_LS(wordcount, PRIVRB_MAXWORDS); // XXX don't be greedy
    HBASSERT_GT(wordcount, 0); // XXX but insist we're getting something..

    //if (debug) HBPVAL(fromnoc0);
    u32 useNoC = 0u;
    if (wordcount > PRIVRB_MAXWORDS) return false;
    
    volatile u32 spin = 0u; // XXX superstitious?
    constexpr u32 MAX_TOTAL_SPIN = 0x10000000;
    while (readNIUReqsOutstanding(useNoC, NRI3_BLOCKING_TRANSACTION_ID) > 0) { // wait til idle
      if (++spin > MAX_TOTAL_SPIN) FAIL(OPERATION_FAILED);
    }
    //if (debug && spin > 0) HBPVAL(spin);
    
    u32 baseaddr = (u32) &privateL1ReadBuffer[0];
    //if (debug) HBXVAL(baseaddr);
    //HBXVAL(baseaddr+sizeof(privateL1ReadBuffer));
    {
      /* ASSUMING THE WORMHOLEB0 RESTRICTIONS APPLY TO BLACKHOLEA0, SINCE
         https://github.com/tenstorrent/tt-isa-documentation/blob/main/BlackholeA0/NoC/Alignment.md
         IS A BLASTED 404 ON Wed Jan 28 11:08:17 2026 
         AND IS STILL 404 ON Sun Feb 22 15:36:47 2026 
         AND IS STILL 404 ON Mon Apr  6 12:11:20 2026 
      */

      u32 sm16 = l1readaddr % 16;
      u32 dm16 = baseaddr % 16;
      //if (debug) HBPVAL(sm16);
      //if (debug) HBPVAL(dm16);
      if (sm16 > dm16) baseaddr += sm16 - dm16;
      else if (sm16 < dm16) baseaddr += 16u + sm16 - dm16;
      //if (debug) HBXVAL(l1readaddr);
      //if (debug) HBXVAL(baseaddr);
      HBASSERT_EQ(baseaddr % 16, sm16);
    }

    while (isNRIBusy(useNoC)) {  
      if (++spin > MAX_TOTAL_SPIN) FAIL(OPERATION_FAILED);
    }
    if (debug && spin > 0) HBPVAL(spin);

    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_LO, l1readaddr); // 32 bit address of source
    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(useNoC,NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(fromnoc0));

    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_LO, baseaddr); // return read value here
    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(useNoC,NRI_NOC_RET_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(usnoc0));

    writeNRIAddress(useNoC,NRI_NOC_PACKET_TAG, NRI3_BLOCKING_TRANSACTION_ID<<10); // set transaction ID
    writeNRIAddress(useNoC,NRI_NOC_CTRL, (0u<<0));          // NOC_CMD_RD

    //    writeNRIAddress(NRI_NOC_AT_LEN_BE, 1u<<2u);      // bytecount to read
    writeNRIAddress(useNoC,NRI_NOC_AT_LEN_BE, wordcount<<2u);      // bytecount to read
    writeNRIAddress(useNoC,NRI_NOC_AT_LEN_BE_1, 0);         // not dealing with masks or etc

    writeNRIAddress(useNoC,NRI_NOC_CMD_CTRL,1);             // initiate write
    readNRIAddress(useNoC,NRI_NOC_CMD_CTRL);                // read for memory ordering

    while (readNIUReqsOutstanding(useNoC, NRI3_BLOCKING_TRANSACTION_ID) > 0) {
      if (++spin > MAX_TOTAL_SPIN) FAIL(OPERATION_FAILED);
    }
    //if (debug) HBPVAL(spin);

    //HBXVAL(l1readaddr);

    HOOKIT();
    volatile u32 *data = (u32*) baseaddr;

    memoryFence();              // FLUSH L0 CACHE BEFORE READING DATA!

    for (u32 i = 0u; i < wordcount; ++i) {
    HOOKIT();
      destaddr[i] = data[i];
    HOOKIT();
      //if (debug) HBXVAL(data[i]);
    }
    //if (debug) HBXVAL(data[0]);
    return true;
  }

  bool NRI3::findBlockCodeInNoC0(U8C ournoc0, U8C theirnoc0, BlockCode bc, ImageBlockAddr & foundiba) {
    //    HBNOTE("FBCN");
    ImageBlockHeader ibh = NRI3::blockingReadImageBlockHeaderNoC0(ournoc0, theirnoc0);
    HBASSERT_EQ(ibh.isValid(),true);
    //HBPTAG(fBCin0,getNameFromImageCode((ImageCode) ibh.getImageCode()));

    //HBPVAL(ournoc0);
    //HBPVAL(theirnoc0);
    
    //  SEARCH THEIR IBAS FOR BC (always on noc0, using a lot more packets & bandwidth than needed, but hey..)
    ImageBlockAddr iba;         // expose outside loop
    for (u32 idx = 0u; idx < ibh.mEntries; ++idx) {
      HOOKIT();
      iba = NRI3::blockingReadImageBlockAddrNoC0(ournoc0, theirnoc0, idx);
      if (!iba.isValid()) {
        HBPVAL(ournoc0);
        HBPVAL(theirnoc0);
        HBXVAL((u32)iba.mIBAMagic);
        HBXVAL((u32)iba.mBlockCode);
        HBXVAL(&iba);
        HBPVAL(idx);
        HBXTAG(PRFVN,estimateStackUsage());
      }
      HOOKIT();
      HBASSERT_EQ(iba.isValid(),true);

      if (iba.mBlockCode == bc) { //  IF FOUND,
        //  SET FOUNDIBA AND RETURN TRUE
        foundiba = iba;
        return true;
      } //else HBXTAG(not,*(u32*)&iba);
    }
    HBPTAG(NTFOD,ibh.isValid());
    HBPVAL(ibh.mEntries);
    HBPVAL(getNameFromBlockCode(bc));
    HBPVAL(iba.isValid());
    HBPVAL(ournoc0);
    HBPVAL(theirnoc0);
    return false; // NOT FOUND
  }

  ImageBlockHeader NRI3::blockingReadImageBlockHeaderNoC0(U8C usNoC0, U8C fromNoC0) {
    //    HBNOTE("BRIBH0");

    ImageBlockHeader ret;       // uninit -> INVALID
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usNoC0);
    //HBPVAL(usct6);
    if (!U8C::onBoardCT6Coord(usct6)) return ret;

    U8C themct6 = U8C::makeCT6CoordFromNoC0Coord(fromNoC0);
    //HBPVAL(themct6);
    if (!U8C::onBoardCT6Coord(themct6)) return ret;

    S8C diffct6(themct6.x-usct6.x,themct6.y-usct6.y);
    return blockingReadImageBlockHeaderCT6Offset(usNoC0,diffct6);
  }

  ImageBlockHeader NRI3::blockingReadImageBlockHeaderCT6Offset(U8C usnoc, S8C ct6off, bool debug) {
    //if (debug) HBNOTE("BRICT");
    ImageBlockHeader ret;  
    ret.reset();                // reset state -> INVALID
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    if (!U8C::onBoardCT6Coord(usct6)) return ret;

    //if (debug) HBPVAL(usct6);
    U8C themct6(usct6.x + ct6off.x,usct6.y + ct6off.y);
    if (!U8C::onBoardCT6Coord(themct6)) return ret;

    //if (debug) HBPVAL(themct6);
    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'

    bool ok = blockingL1ReadCT6(usct6, themct6,
                                (u32) ibux14,
                                sizeof(ImageBlockHeader)>>2u,
                                (u32*) &ret);
    HBASSERT_EQ(ok,true);
    if (debug && !ret.isValid()) HBPVAL(ret.isValid());
    return ret; //< whether read succeeded (then us too) or not (then us neither)
  }

  ImageBlockAddr NRI3::blockingReadImageBlockAddrNoC0(U8C usnoc, U8C fromnoc, u32 ibaindex) {
    ImageBlockAddr ret;       // uninit -> INVALID
    if (!U8C::isNoC0CoordAT6(usnoc)) return ret;
    if (!U8C::isNoC0CoordAT6(fromnoc)) return ret;

    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'
    const u32 *iba = ibux14 + 1u + ibaindex*(sizeof(ImageBlockAddr)>>2u);
    if (false && usnoc == fromnoc) { // XXX short circuit self comm as test
      return *(ImageBlockAddr*) iba;
      //      HBNOTE("NEFRI");
      //      HBPVAL(ibaindex);
      //      HBPVAL(iba);
    }

    bool ok = blockingL1ReadNoC0(usnoc,fromnoc,
                                 (u32) iba,
                                 sizeof(ImageBlockAddr)>>2u,
                                 (u32*) &ret/*,true*/);
    HOOKIT();
    HBASSERT_EQ(ok,true);
    return ret; //< whether read succeeded (then us too) or not (then us neither)
  }

  ImageBlockAddr NRI3::blockingReadImageBlockAddrCT6Offset(U8C usnoc, S8C ct6off, u32 ibaIndex, bool debug) {
    //if (debug) HBNOTE("BR6O");
    ImageBlockAddr ret;       // uninit -> INVALID
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    if (!U8C::onBoardCT6Coord(usct6)) return ret;
    //if (debug) HBPVAL(usct6);

    U8C themct6(usct6.x + ct6off.x,usct6.y + ct6off.y);
    if (!U8C::onBoardCT6Coord(themct6)) return ret;
    //if (debug) HBPVAL(themct6);

    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'
    const u32 *iba = ibux14 + 1u + ibaIndex*(sizeof(ImageBlockAddr)>>2u);

    //    if (debug) HBPVAL(sizeof(ImageBlockAddr)>>2u);
    //if (debug) HBPVAL(iba);
    bool ok = blockingL1ReadCT6(usct6, themct6,
                                (u32) iba,
                                sizeof(ImageBlockAddr)>>2u,
                                (u32*) &ret,
                                debug);
    HBASSERT_EQ(ok,true);
    return ret; //< whether read succeeded (then us too) or not (then us neither)
  }

  s32 NRI3::initiateWriteToHost(U8C sourcenoc0, u32 * sourcedata, u32 wordCount, u64 destaddr) {
    u32 destaddrlow = (u32) (destaddr&0xffffffff);
    u32 destaddrmid = (u32) ((destaddr>>32)&0xffffffff);
    U8C destnoc0 = PCIeTILE_NOC0;
    u32 usenoc = preferNoC(sourcenoc0,destnoc0); 
    waitTilNRIClear(usenoc); // BLOCKING

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_LO, (u32) sourcedata); // 32 bit address of source
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_MID, 0);        // no upper address bits for source
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(sourcenoc0));

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_LO, destaddrlow);   // lower 32 bits of dest
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_MID, destaddrmid); // upper 32 bits of dest
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(destnoc0));

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_PACKET_TAG, 0);          // no DeliverToReceiverOverlay
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_CTRL, (2u<<0));          // NOC_CMD_WR (write, not inline)

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_AT_LEN_BE, wordCount<<2u); // bytecount to write
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_AT_LEN_BE_1, 0);         // not dealing with masks or etc
    
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_CMD_CTRL,1);             // initiate write
    volatile u32 superstition = funcReadNRIAddress(usenoc,3,NRI_NOC_CMD_CTRL); // read for memory ordering

    return 1;
#if 0
      s32 T6ElevatorTransport::initiateWrite(u32 * data, u32 wordCount, U16C xy, u64 destaddr) {
DIEWAY();
    if (!allClear()) return -1; // Not ready
DIEWAY();
    u32 byteCount = wordCount * 4;
    if (byteCount == 0 || byteCount > (1<<14))
      return -2;                // EINVAL: bad size
DIEWAY();

    // TARG (lo+mid) is the source in our L1. TARG hi is our NoC0 coord as NodeId
    u32 targlo = (u32) data;
    u32 targmid = 0u;
    u32 targhi = ((mHostBlockPtr->mPos.x&0x3f)<<6)|(mHostBlockPtr->mPos.y&0x3f);
DIEWAY();

    // In general:
    //   RET (lo+mid) is the dest addr, RET hi is the NoC0 coord of the dest tile
    // For initiateWriteToHost specifically:
    //   RET (lo+mid) is the dest in Host RAM, RET hi is the NoC0 coord of the PCIe tile ?
    u32 retlo = (u32) (destaddr&0xffffffff);
    u32 retmid = (u32) ((destaddr>>32)&0xffffffff);
    u32 rethi = ((xy.y&0x3f)<<6)|(xy.x&0x3f);
    u32 noc_ctrl = (2<<0);      // write request
DIEWAY();

    // Set up the registers
    *NOC_TARG_ADDR_LO = targlo;
    *NOC_TARG_ADDR_MID = targmid;
    *NOC_TARG_ADDR_HI = targhi;

    *NOC_RET_ADDR_LO = retlo;
    *NOC_RET_ADDR_MID = retmid;
    *NOC_RET_ADDR_HI = rethi;
DIEWAY();

    *NOC_CTRL = 2;              // write request
    *NOC_AT_LEN_BE = byteCount;

    // We are ready to initiate the NoC transaction?
DIEWAY();
    *NOC_CMD_CTRL = 1;            // THE BIRD IS AWAY
    u32 readback = *NOC_CMD_CTRL;  // read it back for memory ordering?
DIEWAY();
    return 0;
  }
#endif
    return 0;
  }

  s32 NRI3::initiateWriteToT6(U8C sourcenoc0, u32 * sourcedata, u32 wordCount, U8C destnoc0, u32 destaddr) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC); // nri3 reserved for hNC

    //    HBXTAG(dsar,(u32) destaddr);
    HBASSERT_GT((u32) destaddr, 0x30); // XXX don't deliver to lo mem?
    HBASSERT_LS(wordCount, 2000); // XXX don't be greedy
    HBASSERT_GT(wordCount, 0); // XXX but insist we're getting something..

    MFM_API_ASSERT((wordCount*4u)<=(1u<<14),OUT_OF_RESOURCES); // packets don't go over 16KB for this code
    MFM_API_ASSERT(isInL1(sourcedata),ILLEGAL_STATE);
    MFM_API_ASSERT(isInL1(destaddr), ILLEGAL_ARGUMENT);
    MFM_API_ASSERT((destaddr % 16) == (((u32)sourcedata) % 16), BAD_ALIGNMENT); // rule for small packets L1->L1
    if ((destaddr % 16) != 0) 
      HBPTAG(BHA0-ADDR-WARN,destaddr%16);

    //    u32 usenoc = 0u; // should be useNoC(usnoc0, noc0) when that exists
    u32 usenoc = preferNoC(sourcenoc0,destnoc0); // pick not longer route (all else equal)
    //    SNAP(60,HBPTAG(**USENOC**,(u32) usenoc));
    waitTilNRIClear(usenoc);

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_LO, (u32) sourcedata); // 32 bit address of source
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_MID, 0);        // no upper address bits 
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(sourcenoc0));

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_LO, destaddr);   // 32 bit address of dest
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_MID, 0);        // no upper address bits 
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_RET_ADDR_HI, U8C::makeNoCNodeIdFromNoCCoord(destnoc0));

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_PACKET_TAG, 0);          // no DeliverToReceiverOverlay
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_CTRL, (2u<<0));          // NOC_CMD_WR (write, not inline)

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_AT_LEN_BE, wordCount<<2u); // bytecount to write
    funcWriteNRIAddress(usenoc, 3, NRI_NOC_AT_LEN_BE_1, 0);         // not dealing with masks or etc

    funcWriteNRIAddress(usenoc, 3, NRI_NOC_CMD_CTRL,1);             // initiate write
    volatile u32 superstition = funcReadNRIAddress(usenoc,3,NRI_NOC_CMD_CTRL); // read for memory ordering

    return 1;
  }

  void NRI3::waitTilNRIClear(u8 noc) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC); // nri3 reserved for hNC
    u32 count = 0u;
    while (isNRIBusy(noc)) {
      if (++count == 0u) FAIL(IO_ERROR);
    }
  }
  
}

