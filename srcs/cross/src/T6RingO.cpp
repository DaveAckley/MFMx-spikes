#include "T6RingO.h"
#include "Fail.h"
#include "CrossUtils.h"
#include "Printf.h"
#include "FastLocal.h" // for FAll
#include "FastT2.h" // for create

namespace MFM {

  /** T6-wide NoC x NRI Assignments

            NoC 0                 NoC 1
        +-------------------+ +-------------------+
    NRI0|PCIe comm on       | | unassigned        |
        |T6Elevator         | |[Future PCIe       |
        |Platform           | | comm ?]           |
        |                   | |                   |
        +-------------------+ +-------------------+
    NRI1|T6RingO SE Corner  | |T6RingO NW Corner  |
        |@NOC0_ROUTER_CFG_2 | |@NOC1_ROUTER_CFG_2 |
        |writes EAST to SW  | |writes WEST to NE  |
        |(NOC0_ROUTER_CFG_4)| |(NOC1_ROUTER_CFG_4)|
        +-------------------+ +-------------------+
    NRI2|T6RingO SW Corner  | |T6RingO NE Corner  |
        |@NOC0_ROUTER_CFG_4 | |@NOC1_ROUTER_CFG_4 |
        |writes SOUTH to NW | |writes NORTH to SE |
        |(NOC1_ROUTER_CFG_2)| |(NOC0_ROUTER_CFG_2)|
        +-------------------+ +-------------------+
    NRI3| unassigned        | | unassigned        |
        |                   | |                   |
        |                   | |                   |
        |                   | |                   |
        +-------------------+ +-------------------+
   */

  static constexpr u32 NOC0_ROUTER_CFG_2 = 0xffb2'010C; //< stg for state incoming to SE
  static constexpr u32 NOC0_ROUTER_CFG_4 = 0xffb2'0114; //< stg for state incoming to SW
  static constexpr u32 NOC1_ROUTER_CFG_2 = 0xffb3'010C; //< stg for state incoming to NW
  static constexpr u32 NOC1_ROUTER_CFG_4 = 0xffb3'0114; //< stg for state incoming to NE

  static char * tInfo(U8C noc0c, u32 destaddr) {
    static char buf[12];
    const char * corner;
    if (destaddr == NOC0_ROUTER_CFG_2) corner = "SE";
    else if (destaddr == NOC0_ROUTER_CFG_4) corner = "SW";
    else if (destaddr == NOC1_ROUTER_CFG_2) corner = "NW";
    else if (destaddr == NOC1_ROUTER_CFG_4) corner = "NE";
    else corner = "??";
    snprintf(buf,12,"T%d%s:",
             U8C::makeTLBIFromNoC0Coord(noc0c),
             corner);
    return buf;
  }
  static bool partOfACompleteRing(Corner4 c4, u32 tlbi) {
    for (u32 t = 0u; t < 4u; ++t) {
      Dir4 tonext = clockwiseDir4FromCorner4(c4);
      u32 nexttlbi;
      if (!U8C::makeNgbTLBIInDir(tonext,tlbi,nexttlbi))
        return false;                 // If no such ngb, no ring here
      c4 = clockwiseCorner4(c4);
      tlbi = nexttlbi;
    }
    return true;
  }

  void T6RingCorner::writeNIUAddress(u32 byteoffset, u32 value) const {
    volatile u32 * p = getNIUAddress(byteoffset);
    DP.printf("T6RCwNA n%d r%d %s*0x%08x=0x%08x,%d\n",
              mNoC, mNRI,
              c4Info(),
              (u32) p, value, value);
    *p = value;
    return; // XX DEBUG
  }

  void T6RingCorner::writeNRIAddress(u32 byteoffset, u32 value) const {
    volatile u32 * p = getNRIAddress(byteoffset);
    if (byteoffset == 0)
      DP.printf("wNRI %d.%d %s+0:%x,%d\n",
                mNoC, mNRI, c4Info(),
                value, value);
    else if (byteoffset == 8) {
      U8C dc = U8C::makeU8CFromNoCNodeId(value);
      DP.printf("wNRI %d.%d %s%+8:%x,%d(%d,%d)\n",
                mNoC, mNRI, c4Info(),
                value, value,
                dc.x,dc.y);
    }
    *p = value;
  }

  char * T6RingCorner::c4Info() const {
    static char buf[12];
    snprintf(buf,12,"T%d%s:",
             U8C::makeTLBIFromNoC0Coord(fAll.mPos),
             corner4ToByteString(mRingCorner));
    return buf;
  }

  void T6RingCorner::init(Corner4 c4) {
    DP.printf("T6RCinit(%d)%04d\n",
              c4,create(1000));
    if (configureSelf(c4))
      configureNoC();
  }

  bool T6RingCorner::configureSelf(Corner4 c4) {
    memset_s(this, 0u, sizeof(*this));

    U8C ournoc0c =  fAll.mPos;
    u32 ourtlbi = U8C::makeTLBIFromNoC0Coord(ournoc0c);

    mRingCorner = c4;
    mSourceCoord = ournoc0c;

    mRingExists = partOfACompleteRing(c4, ourtlbi);

    DP.printf("CcS %s%s\n",
              c4Info(), mRingExists?"REX":"RNE");
    if (!mRingExists) return false;

    mLastState.init();
    //mLastState.mWord = 1u; // XXX DEBUG
    mNewState.init();

    u32 ngbtlbi;
    switch (mRingCorner) {
    case C4_SE:  // SE -> E -> SW
      if (!U8C::makeNgbTLBIInDir(D4_E, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 0u; mNRI = 1u;
      mDestCoord = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = NOC0_ROUTER_CFG_2;
      mDestAddress = NOC0_ROUTER_CFG_4;
      break;

    case C4_SW: // SW -> S -> NW
      if (!U8C::makeNgbTLBIInDir(D4_S, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 0u; mNRI = 2u;
      mDestCoord = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = NOC0_ROUTER_CFG_4;
      mDestAddress = NOC1_ROUTER_CFG_2;
      break;

    case C4_NW: // NW -> W -> NE
      if (!U8C::makeNgbTLBIInDir(D4_W, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 1u; mNRI = 1u;
      mDestCoord = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = NOC1_ROUTER_CFG_2;
      mDestAddress = NOC1_ROUTER_CFG_4;
      break;

    case C4_NE: // NE -> N -> SE
      if (!U8C::makeNgbTLBIInDir(D4_N, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 1u; mNRI = 2u;
      mDestCoord = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = NOC1_ROUTER_CFG_4;
      mDestAddress = NOC0_ROUTER_CFG_2;
      break;

    default:
      FAIL(UNREACHABLE_CODE);
    }
    if (mNoC == 1u) {
      mSourceCoord = U8C::makeU8CRawT6CoordFromOtherNoC(mSourceCoord);
      mDestCoord = U8C::makeU8CRawT6CoordFromOtherNoC(mDestCoord);
    }
    DP.printf("CcSX %d.%d %s s(%d,%d) d(%d,%d)=%d\n",
              mNoC,mNRI,
              c4Info(),
              mSourceCoord.x,mSourceCoord.y,
              mDestCoord.x,mDestCoord.y,
              U8C::makeNoCNodeIdFromU8CCoord(mDestCoord));

    return true;
  }

  /** NoC byte offsets (arg to getNIUAddress) */
  static constexpr u32 NOC_NODE_ID = 0x44;

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

  void T6RingCorner::configureNoC() {
    if (!mRingExists) FAIL(ILLEGAL_STATE);

    DP.printf("cfnoc %d.%d %s@0x%08x da:*0x%08x (%d,%d)\n",
              mNoC, mNRI,
              c4Info(),
              (u32) getNRIAddress(0),
              mDestAddress,
              mDestCoord.x, mDestCoord.y
              );

    /// NIU level
    /* nothing? */

    /// NRI level
    waitTilNRIClear();
    
    writeNRIAddress(NRI_NOC_TARG_ADDR_LO, mDestAddress); // lower 32 address
    writeNRIAddress(NRI_NOC_TARG_ADDR_MID, 0);           // upper address bits 0
    writeNRIAddress(NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromU8CCoord(mDestCoord)); 

    // NOC_RET_ADDR not used by inline write; left unconfigured

    // Clear gunk
    writeNRIAddress(NRI_NOC_PACKET_TAG, 0);  // no DeliverToReceiverOverlay

    // Set transaction type
    writeNRIAddress(NRI_NOC_CTRL, (2u<<0)|(1u<<3));      // NOC_CMD_WR | NOC_CMD_WR_INLINE

    // Clear more gunk
    writeNRIAddress(NRI_NOC_AT_LEN_BE, 0);   // ignored on inline mmio write
    writeNRIAddress(NRI_NOC_AT_LEN_BE_1, 0); // ignored on inline mmio write
    
    // LEFT TO BE CONFIGURED AT XMIT TIME: NOC_AT_DATA
    return;
  }

  void T6RingCorner::waitTilNRIClear() const {
    u32 count = 0u;
    while (readNRIAddress(NRI_NOC_CMD_CTRL) & 1) {
      if (++count == 0u) FAIL(IO_ERROR);
    }
  }

  void T6RingCorner::sendDownstream(u32 val) const {
    waitTilNRIClear();
    volatile u32 * regaddr = getNRIAddress(NRI_NOC_TARG_ADDR_LO);
    u32 targaddr = readNRIAddress(NRI_NOC_TARG_ADDR_LO);
    DP.printf("SD %s%d.%d->%s%x/%d\n",
              c4Info(), mNoC,mNRI,
              tInfo(mDestCoord,targaddr),
              val,val);
    writeNRIAddress(NRI_NOC_AT_DATA,val);  // stash value to send
    writeNRIAddress(NRI_NOC_CMD_CTRL,1);   // initiate write
    u32 r = readNRIAddress(NRI_NOC_CMD_CTRL);      // 'defensive readback'?
  }

  u32 T6RingCorner::readUpstream() const {
    u32 val = *(volatile u32 *) mSourceAddress;
    if (false && val!=0)
      DP.printf("RUP:%s%08x<*%08x\n",
                c4Info(),
                val,(u32) mSourceAddress);
    return val;
  }

  void T6RingCorner::doPropagate() {
    sendDownstream(mNewState.mWord);
    mLastState = mNewState;
  }

  void T6RingCorner::doActive() {
    ++mTimesActive;
    mNewState.mState[mRingCorner] += 4u; // pass the token
    if (true)
      DP.printf("dAc %s%dK:%08x\n",
                c4Info(),
                mTimesActive/1000u,
                mNewState.mWord);
    doPropagate();
  }

  bool T6RingCorner::doAnchorReset(u32 newcs) {
    if (mAnchorCounter == 0u) {
      mResetPhase = 0u;
      mAnchorCounter = U16_MAX;
      DP.printf("dAR %s%08x\n", c4Info(),newcs);
    } else --mAnchorCounter;

    u8 tlbi = U8C::makeTLBIFromNoC0Coord(fAll.mPos);
    if (tlbi == 0u) tlbi = 0xee; // avoid 0 with OoB but recognizable hex val
    switch (mResetPhase) {
    case 0u:                    // enter reset 
      DP.printf("RP:%d%s%c\n",
                mResetPhase,
                c4Info(),
                mAnchorCounter>U16_MAX/2 ? '+' : '-');
      mResetPhase = 1u;
      mAnchorCounter = U16_MAX;
      sendDownstream(tlbi); // send first signal
      return true;

    case 1u:                    // wait for first signal
      if (newcs == tlbi) {
        DP.printf("RP:%d%s%d\n",
                  mResetPhase,
                  c4Info(),
                  mAnchorCounter);
        mResetPhase = 2u;
        mAnchorCounter = U16_MAX;
        sendDownstream((tlbi<<8) | tlbi); // send second signal
      } // else just wait
      return true;
    case 2u:                    // wait for second signal
      if (newcs == ((tlbi<<8) | tlbi)) {
        DP.printf("RP:%d%s%d\n",
                  mResetPhase,
                  c4Info(),
                  mAnchorCounter);
        mResetPhase = 3u;
        mAnchorCounter = U16_MAX;
        sendDownstream(0x00'01'02'03); // send valid state
      } // else just wait
      return true;
    case 3u:      
      if (newcs == 0x00'01'02'03) { // exit reset
        DP.printf("RP:%d%sn0x%08x\n",
                  mResetPhase,
                  c4Info(),
                  newcs);
        mResetPhase = 0u;
        mAnchorCounter = U16_MAX;
        mLastState.mWord = newcs;
        DP.printf("T6RCdARxr %s(%d,%d)",
                  c4Info(),
                  fAll.mPos.x,fAll.mPos.y);
        return false;
      } // else just wait
      return true;

    default:
      FAIL(ILLEGAL_STATE);
    }
  }

  // OK finally actual ring oscillator logic
  void T6RingCorner::update() {
    if (!mRingExists) return; // mustard in shoe
    //DP.printf("T6RCUP(%d,%d)",fAll.mPos.x,fAll.mPos.y);

    CornerState temp;
    temp.mWord = readUpstream(); // see if any news
    if (temp.mWord != mLastState.mWord)
      DP.printf("RUT:%s[%08x]%08x>>*%08x\n",
                c4Info(),
                (u32) mSourceAddress,
                mLastState.mWord, temp.mWord);

    Corner4 mincorner = (Corner4) 4u;
    bool valid = temp.isValid(mincorner);
    bool anchor = (mRingCorner == C4_SE);
    //DP.printf("[%s%da%dr%d]\n",corner4ToByteString(mRingCorner),valid,anchor,mResetPhase);
      {
        static u32 spin = 0;
        if (++spin % 1'000'000 == 0u)
          DP.printf("UP11(%d,%d)%d %dM m%d v%d a%d n0x%08x\n",
                    fAll.mPos.x,fAll.mPos.y,mRingCorner,
                    spin/1'000'000,
                    mincorner, valid, anchor, temp.mWord);
      }

      // return; // XXX DEBUG

    if (anchor && (!valid || mResetPhase != 0))
      if (doAnchorReset(temp.mWord))
        return; // ELSE FALL THROUGH

    //     return; // XXX DEBUG

    mNewState.mWord = temp.mWord;
    if (valid && mincorner == mRingCorner)
      return doActive(); // active means valid and we're min

    if (mNewState.mWord != mLastState.mWord) {
      {
        static u32 spin = 0;
        if (spin++ % 1'000 == 0u)
          DP.printf("UP12(%d,%d)%dK v%d a%d n0x%08x\n",
                    fAll.mPos.x,fAll.mPos.y,
                    spin/1'000,
                    valid, anchor, mNewState.mWord);
      }

      doPropagate();     // propagate if new (even if invalid)
    }
  }

  void T6RingOscillators::init() {
    DP.printf("T6ROinit\n");
    for (u32 c4 = 0u; c4 < 4u; ++c4)
      mT6RingCorners[c4].init((Corner4) c4);
  }

  void T6RingOscillators::update() {
    for (u32 c4 = 0u; c4 < 4u; ++c4)
      mT6RingCorners[c4].update();
  }
  
}
