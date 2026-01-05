#include "T6RingO.h"
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

  static char * tInfo(U8C noc0c, u32 destaddr) {
    static char buf[12];
    const char * corner;
    switch (destaddr) {
    case INBOUND_TO_SE: corner = "SE"; break;
    case INBOUND_TO_SW: corner = "SW"; break;
    case INBOUND_TO_NW: corner = "NW"; break;
    case INBOUND_TO_NE: corner = "NE"; break;
    default: corner = "??"; break;
    }
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

  void funcWriteNRIAddress(u32 noc, u32 nri, u32 byteOffset, u32 value) {
    volatile u32 * p = funcGetNRIAddress(noc, nri, byteOffset);
    if (false)
      P.printf("FWNA 0x%08x=%d/0x%x\n", (u32) p, value, value);
    *p = value;
  }

  bool NRI3::initiateWrite() {
    if (isNRIBusy()) return false;

    P.printf("NRIW:%d+%d S%08x(%d,%d) D%08x(%d,%d)\n",
             mNoC, mWordCount,
             mSourceL1, mSourceCoord0.x, mSourceCoord0.y,
             mDestL1, mDestCoord0.x, mDestCoord0.y);

    writeNRIAddress(NRI_NOC_TARG_ADDR_LO, mSourceL1); // 32 bit address of source
    writeNRIAddress(NRI_NOC_TARG_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromU8CCoord(mSourceCoord0));

    writeNRIAddress(NRI_NOC_RET_ADDR_LO, mDestL1);   // 32 bit address of dest
    writeNRIAddress(NRI_NOC_RET_ADDR_MID, 0);        // no upper address bits 
    writeNRIAddress(NRI_NOC_RET_ADDR_HI, U8C::makeNoCNodeIdFromU8CCoord(mDestCoord0));

    writeNRIAddress(NRI_NOC_PACKET_TAG, 0);          // no DeliverToReceiverOverlay
    writeNRIAddress(NRI_NOC_CTRL, (2u<<0));          // NOC_CMD_WR (write, not inline)

    writeNRIAddress(NRI_NOC_AT_LEN_BE, mWordCount<<2u); // bytecount to write
    writeNRIAddress(NRI_NOC_AT_LEN_BE_1, 0);         // not dealing with masks or etc

    writeNRIAddress(NRI_NOC_CMD_CTRL,1);             // initiate write
    return true;
  }

  u32 CornerEW::load(S8C center, Corner4 c4) {
    mEWOriginCC = center; // in corner coords
    return loadStore(c4,true);
  }

  u32 CornerEW::store(S8C center, Corner4 c4) {
    mEWOriginCC = center; // in corner coords
    return loadStore(c4,false);
  }

  u32 CornerEW::loadStore(Corner4 c4, bool doLoad) {
    // Iterate over ew coords.
    MDist4 md;
    u32 count = mSitesClaimed;;
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint ewc = md.GetPoint(sn);    // ew coord..
      S8C cc = mEWOriginCC + S8C(ewc); // ..as corner coord offset 
      U8C tc;
      if (!T6Grid::cornerCoordToTileCoordIfAny(cc,c4,tc)) continue;
      if (doLoad) {
        mEW.getAtom(sn) = theT6Grid.getAtom(tc);
        P.printf("%s:LEW%2d[%d,%d]=(%u,%u)\n",
                 corner4ToByteString(c4),
                 mSitesClaimed,
                 ewc.GetX(),ewc.GetY(),
                 tc.x,tc.y);
      } else /* doStore */ 
        theT6Grid.getAtom(tc) = mEW.getAtom(sn);
      ++mSitesClaimed;
    }
    return mSitesClaimed-count;
  }

  void CornerEW::init() {
    mHeader = CarSig();
    mHeader.mCarNonce = createBits(8);
    mEW.reset();
    mWaister = mHeader;
    mEWTag.mWord = 0u;          // init me later
    mEWOriginCC = S8C(0,0);       // ditto
    mEWCommand = 0u;
    mSitesClaimed = 0u;
    mFooter = mHeader;
  }

  bool CornerEW::isComplete() const {
    return
      mHeader == mWaister &&
      mHeader == mFooter;
  }

  void T6RingCorner::writeNIUAddress(u32 byteoffset, u32 value) const {
    volatile u32 * p = getNIUAddress(byteoffset);
    P.printf("T6RCwNA n%d r%d %s*0x%08x=0x%08x,%d\n",
              mNoC, mNRI,
              c4Info(),
              (u32) p, value, value);
    *p = value;
    return; // XX DEBUG
  }

  void T6RingCorner::writeNRIAddress(u32 byteoffset, u32 value) const {
    volatile u32 * p = getNRIAddress(byteoffset);
    if (byteoffset == 0)
      P.printf("wRA %d.%d %s+0:%x,%d\n",
                mNoC, mNRI, c4Info(),
                value, value);
    else if (byteoffset == 8) {
      U8C dc = U8C::makeU8CFromNoCNodeId(value);
      P.printf("wRA %d.%d %s+8:%x,%d(%d,%d)\n",
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
    P.printf("I[%s]\n", corner4ToByteString(c4));
    if (configureSelf(c4))
      configureNoC();
  }

  CornerEW cornerEWs[4];        // Source/dest EW per corner

  bool T6RingCorner::configureSelf(Corner4 c4) {
    memset_s(this, 0u, sizeof(*this));

    U8C ournoc0c =  fAll.mPos;
    u32 ourtlbi = U8C::makeTLBIFromNoC0Coord(ournoc0c);

    mRingCorner = c4;
    mSourceCoord0 = ournoc0c;

    mRingExists = partOfACompleteRing(c4, ourtlbi);

    P.printf("CcS %s%s\n",
              c4Info(), mRingExists?"REX":"RNE");
    if (!mRingExists) return false;

    mActiveCounter = 0u;
    mTotalWrites = 0u;

    mLastRcvd.init();
    mLastSent.init();

    u32 ngbtlbi;
    switch (mRingCorner) {
    case C4_SE:  // SE -> E -> SW
      if (!U8C::makeNgbTLBIInDir(D4_E, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 0u; mNRI = 1u;
      mDestCoord0 = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = INBOUND_TO_SE;
      mDestAddress = INBOUND_TO_SW;
      break;

    case C4_SW: // SW -> S -> NW
      if (!U8C::makeNgbTLBIInDir(D4_S, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 0u; mNRI = 2u;
      mDestCoord0 = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = INBOUND_TO_SW;
      mDestAddress = INBOUND_TO_NW;
      break;

    case C4_NW: // NW -> W -> NE
      if (!U8C::makeNgbTLBIInDir(D4_W, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 1u; mNRI = 1u;
      mDestCoord0 = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = INBOUND_TO_NW;
      mDestAddress = INBOUND_TO_NE;
      break;

    case C4_NE: // NE -> N -> SE
      if (!U8C::makeNgbTLBIInDir(D4_N, ourtlbi, ngbtlbi)) FAIL(ILLEGAL_STATE);
      mNoC = 1u; mNRI = 2u;
      mDestCoord0 = U8C::makeU8CNoCCoordFromTLBI(ngbtlbi);
      mSourceAddress = INBOUND_TO_NE;
      mDestAddress = INBOUND_TO_SE;
      break;

    default:
      FAIL(UNREACHABLE_CODE);
    }

    P.printf("CcSX %d.%d %s s(%d,%d) d(%d,%d)=%d\n",
              mNoC,mNRI,
              c4Info(),
              mSourceCoord0.x,mSourceCoord0.y,
              mDestCoord0.x,mDestCoord0.y,
              U8C::makeNoCNodeIdFromU8CCoord(mDestCoord0));

    P.printf("%sPEWI\n",c4Info());
    mShipEWConfig.mSourceL1 = (u32) &cornerEWs[mRingCorner];
    mShipEWConfig.mDestL1 = (u32) &cornerEWs[clockwiseCorner4(mRingCorner)];
    mShipEWConfig.mWordCount = sizeof(cornerEWs[mRingCorner])>>2u;
    mShipEWConfig.mSourceCoord0 = mSourceCoord0;
    mShipEWConfig.mDestCoord0 = mDestCoord0;
    mShipEWConfig.mNoC = mNoC;
    P.printf("%sCEWI:%02x\n",c4Info(),cornerEWs[mRingCorner].mHeader.mCarNonce);

    return true;
  }

  void T6RingCorner::configureNoC() {
    if (!mRingExists) FAIL(ILLEGAL_STATE);

    /// NIU level
    /* nothing? */

    /// NRI level
    waitTilNRIClear();

    writeNRIAddress(NRI_NOC_TARG_ADDR_LO, mDestAddress); // lower 32 address
    writeNRIAddress(NRI_NOC_TARG_ADDR_MID, 0);           // upper address bits 0

    /// Trying to simplify the logic, we're maintaining all coords
    /// relative to NoC0 as much as possible. But when we go to the
    /// NIU and NRI configuration registers we have to be official.

    /// Mon Dec 22 00:28:15 2025 Except NO, doh, arrgh, doh: The
    /// firmware already does this remapping by default. Doh.

    U8C destOnNoC = mDestCoord0;
    if (/*doh*/false && mNoC != 0u)
      destOnNoC = U8C::makeU8CRawT6CoordFromOtherNoC(destOnNoC);
    writeNRIAddress(NRI_NOC_TARG_ADDR_HI, U8C::makeNoCNodeIdFromU8CCoord(destOnNoC));

    u32 niu_cfg_0 = readNIUAddress(0x100);
    P.printf("cfnoc %d.%d %x %s@0x%08x da:*0x%08x (%d,%d) DON (%d,%d)\n",
             mNoC, mNRI,
             niu_cfg_0,
             c4Info(),
             (u32) getNRIAddress(0),
             mDestAddress,
             mDestCoord0.x, mDestCoord0.y,
             destOnNoC.x, destOnNoC.y
             );


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

  void T6RingCorner::reportRegisterConfiguration(Printer & prt) const {
    u32 nriaddr = (u32) getNRIAddress(0);
    prt.printf("%s +CNF+: %d.%d 0x%08x[[\n",c4Info(),mNoC,mNRI,nriaddr);
    for (u32 b = 0u; b<=0x8; b+=4u) {
      u32 c = readNRIAddress(b);
      prt.printf("+%02x:%x,%d\n", b, c);
    }
    prt.printf("%s -CNF-: %d.%d]]\n",c4Info());
  }

  void T6RingCorner::sendDownstream(u32 val) {
    waitTilNRIClear();
    volatile u32 * regaddr = getNRIAddress(NRI_NOC_TARG_ADDR_LO);
    u32 targaddr = readNRIAddress(NRI_NOC_TARG_ADDR_LO);
    P.printf("SD %s%d.%d->%s%08x\n",
              c4Info(), mNoC,mNRI,
              tInfo(mDestCoord0,targaddr),
              val);
    if (mSourceCoord0.x == 2 && mSourceCoord0.y == 2 &&
        mRingCorner == C4_SW && val == 0xee) {
      DP.printf("SD!!%sHEAH\n",c4Info());
      reportRegisterConfiguration(DP);
    }
    writeNRIAddress(NRI_NOC_AT_DATA,val);  // stash value to send
    writeNRIAddress(NRI_NOC_CMD_CTRL,1);   // initiate write
    ++mTotalWrites;                        // record we did that
  }

  u32 T6RingCorner::readUpstream() const {
    u32 val = *(volatile u32 *) mSourceAddress;
    if (false && val!=0)
      P.printf("RUP:%s%08x<*%08x\n",
                c4Info(),
                val,(u32) mSourceAddress);
    return val;
  }

  void T6RingCorner::doPropagate(CornerState cs) {
    sendDownstream(cs.mWord);
    mLastSent = cs;
  }

  void T6RingCorner::createEW(CornerState cs) {
    S8C center;
    center = S8C(between(-5,4),between(-5,4)); 

    CornerEW & cew = cornerEWs[mRingCorner];
    cew.mSitesClaimed = 0u;
    cew.mEWTag = cs;
    P.printf("%sCREW+(%d,%d):%08x\n",c4Info(),center.x,center.y,cew.mEWTag.mWord);
    u32 sites = cew.load(center,mRingCorner);
    P.printf("%sCREWS=%d:%08x\n",c4Info(),sites,cew.mEWTag.mWord);
  }

  void T6RingCorner::sendEW() {
    CornerEW & cew = cornerEWs[mRingCorner];
    P.printf("%sSDEW=%d %08x\n",c4Info(),
             cew.mSitesClaimed,cew.mEWTag);
    while (!mShipEWConfig.initiateWrite()) {
      P.printf(".");
    }
    P.printf("%sSDEW- %08x\n",c4Info(),cew.mEWTag);
  }

  void T6RingCorner::enterActive() {
    ++mTimesActive;
    mActiveCounter = 10u;// U16_MAX; //between(5000,50000);
    P.printf("%sENAC:%d:%08x\n",c4Info(),mTimesActive,
             mLastSent.mWord);
    doPropagate(mLastRcvd);
  }

  void T6RingCorner::exitActive() {
    P.printf("%sEXAC:%d:%08x->",
             c4Info(),mTimesActive,
             mLastRcvd.mWord);
    CornerState cs = mLastRcvd;
    cs.passToken();             // jump mincorner state to be largest
    P.printf("%08x\n",cs.mWord);
    doPropagate(cs);
  }

  void T6RingCorner::doPassive() {
    u32 phase = mLastRcvd.getPhaseGap();
    P.printf("%sDOPS:*%08x P%d\n",c4Info(),
             mLastRcvd.mWord,
             phase);
    CornerEW & cew = cornerEWs[mRingCorner];
    switch (phase) {
    case 2:
      if (cew.mEWTag.mWord == mLastRcvd.mWord) {
        P.printf("%sPPEW(%d,%d)+%d\n",c4Info(),
                 cew.mEWOriginCC.x,cew.mEWOriginCC.y,
                 cew.mSitesClaimed);
        u32 sites = cew.load(cew.mEWOriginCC,mRingCorner);
        P.printf("%sSPEW(%d,%d)+%d=%d\n",c4Info(),
                 cew.mEWOriginCC.x,cew.mEWOriginCC.y,
                 sites,cew.mSitesClaimed);
        sendEW();               // pass it on (maybe with our additions)
        doPropagate(mLastRcvd);
      } else P.printf("%sDOX: %d %08x\n",c4Info(),
                      cornerEWs[mRingCorner].isComplete(),
                      cornerEWs[mRingCorner].mEWTag.mWord);
      break;
    case 3:
      if (cew.mEWTag.mWord == mLastRcvd.mWord) {
        P.printf("%sSTEW(%d,%d)+%d\n",c4Info(),
                 cew.mEWOriginCC.x,cew.mEWOriginCC.y,
                 cew.mSitesClaimed);
        u32 sites = cew.store(cew.mEWOriginCC,mRingCorner);
        P.printf("%sPSTEW(%d,%d)+%d=%d\n",c4Info(),
                 cew.mEWOriginCC.x,cew.mEWOriginCC.y,
                 sites,cew.mSitesClaimed);
        sendEW();               // pass it on (untouched by us)
        doPropagate(mLastRcvd);
      } else P.printf("%sDOST: %d %08x\n",c4Info(),
                      cornerEWs[mRingCorner].isComplete(),
                      cornerEWs[mRingCorner].mEWTag.mWord);
      break;
    default:
      doPropagate(mLastRcvd);
    }
  }

  void T6RingCorner::doActive() {
    // enter on stability, meaning passives have all responded to our
    // previous gap propagation
    u32 gap = mLastRcvd.getPhaseGap();
    CornerEW & cew = cornerEWs[mRingCorner];
    if (gap > 3) exitActive();
    else if (mActiveCounter == 0u) {
      P.printf("%sGAP%d\n",c4Info(),gap);
      mActiveCounter = 10u;//U16_MAX;//between(5000,50000);
      CornerState cur = mLastRcvd;
      if (!cur.incrementPhaseGap())
        P.printf("%sINCFAIL%d\n",c4Info(),gap);
      if (gap==1u) { // meaning we're about to send a fill-EW
        createEW(cur);
        sendEW();
      } else if (gap==2u) { // meaning we need to do the EWT
        P.printf("%sEWRT(%d,%d)+%d %08x %s\n",c4Info(),
                 cew.mEWOriginCC.x,cew.mEWOriginCC.y,
                 cew.mSitesClaimed,
                 cew.mEWTag.mWord,
                 cew.isComplete()?"COMP":"INCO");
        // XXX DO THE FOGGIN TRANSITION
        // XXX WALA THE FOGGIN TRANSITION IS NOW DONE
        cew.mEWTag = cur;       // update ewt
        sendEW();               // and send it around again
      } else P.printf("%sDRX: %d %08x\n",c4Info(),
                      cew.isComplete(),
                      cew.mEWTag.mWord);
      doPropagate(cur);
    } else --mActiveCounter;
  }

  bool T6RingCorner::doAnchorReset() {
    u32 newcs = mLastRcvd.mWord;
    if (mAnchorCounter == 0u) {
      mResetPhase = 0u;
      mAnchorCounter = U16_MAX;
      P.printf("dAR %s%08x\n", c4Info(),newcs);
    } else --mAnchorCounter;

    u32 sig1 = U8C::makeTLBIFromNoC0Coord(fAll.mPos); // use tlbi as signal
    if (sig1== 0u) sig1 = 0xf0|mRingCorner; // avoid 0 with OoB but recognizable hex val
    u32 sig2 = (sig1<<8) | sig1; // double sig1 as sig2
    u8 acch = mAnchorCounter>U16_MAX/2 ? '+' : '-';

    CornerState cur;
    switch (mResetPhase) {
    case 0u:                    // enter reset
      P.printf("RP:%d%s%c\n",
               mResetPhase,
               c4Info(),
               acch);
      mResetPhase = 1u;
      mAnchorCounter = U16_MAX;
      cur.mWord = sig1;
      doPropagate(cur);         // send first signal
      return true;

    case 1u:                    // wait for first signal
      if (newcs == sig1) {
        P.printf("[%08x]->RP:%d%s%c\n",
                 newcs,
                 mResetPhase,
                 c4Info(),
                 acch);
        mResetPhase = 2u;
        mAnchorCounter = U16_MAX;
        cur.mWord = sig2;
        doPropagate(cur);       // send second signal
      } // else just wait
      return true;
    case 2u:                    // wait for second signal
      if (newcs == sig2) {
        P.printf("[%08x]->RP:%d%s%c\n",
                 newcs,
                 mResetPhase,
                 c4Info(),
                 acch);
        mResetPhase = 3u;
        mAnchorCounter = U16_MAX;
        cur.mWord = 0x01'02'03'00; // valid state with SE active
        doPropagate(cur);          // send valid state

      } // else just wait
      return true;
    case 3u:
      if (newcs == 0x01'02'03'00) { // exit reset
        P.printf("[%08x]->RP:%d%s\n",
                 newcs,
                 mResetPhase,
                 c4Info());
        mResetPhase = 0u;
        mAnchorCounter = U16_MAX;
        //??mLastState.mWord = newcs;
        P.printf("%sXRST!\n",
                  c4Info());
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

    //=== Update rcvd
    {
      CornerState temp;
      temp.mWord = readUpstream();           // see if any news
      if (mLastRcvd.dominates(temp)) return; // get some new stuff you schmos
      mLastRcvd = temp;                      // we have news (or invalidity)
    }
    //>>> mLastRcvd is updated (may or may not be valid)

    //=== Handle invalid states
    Corner4 mincorner;
    if (!mLastRcvd.isValid(mincorner)) {
      if (mRingCorner == C4_SE) // If I am the ring anchor
        doAnchorReset();        // go deal with this
      else
        propagateDiff();        // else push changes
      return;
    }
    //>>> mLastRcvd is valid (and so is mincorner)

    //=== Handle input dominated by our output
    if (mLastSent.dominates(mLastRcvd))
      return;
    //>>> mLastRcvd is not dominated by mLastSent

    bool active = (mincorner == mRingCorner);

    //=== Handle stable signal
    if (mLastRcvd.mWord == mLastSent.mWord) {
      if (active) doActive();   // active acts when stability
      return;                   // while passive does nothing
    }
    //>>> mLastRcvd != mLastSent

    P.printf("RNU:%s%s%08x >> %08x\n", //Received New Update?
             active?"*":"",
             c4Info(),
             mLastSent.mWord,
             mLastRcvd.mWord);

    //=== Handle changing signal
    if (!mLastSent.isValid() ||           // transition to valid (exit reset)
        mLastRcvd.dominates(mLastSent)) { // or ordinary advancement
      if (active) enterActive();
      else doPassive();
      return;
    }
    //>>> Initiating or dominating change handled

    if (mLastSent.dominates(mLastRcvd)) {
      if (active) return;       // drop it, waiting for ring response
      else {
        // should not happen:
        P.printf("PDI:%s%08x << %08x\n", // Passive Dominates Input
                 c4Info(),
                 mLastSent.mWord,
                 mLastRcvd.mWord);
      }
      return;
    }
    //>>> Subordinate change handled

    P.printf("URC:%s%08x ?? %08x\n", // "Unreachable code"
             c4Info(),
             mLastRcvd.mWord,
             mLastSent.mWord);
    FAIL(UNREACHABLE_CODE);
  }

  void T6RingOscillators::init() {
    mLastUpdateMillis = millisElapsed();
    P.printf("T6ROinit\n");
    for (u32 c4 = 0u; c4 < 4u; ++c4) {
      cornerEWs[c4].init();
      mT6RingCorners[c4].init((Corner4) c4);
    }
  }

  void T6RingOscillators::update() {
    u32 nowms;
    while (millisFrom(mLastUpdateMillis,nowms = millisElapsed()) < GATE_DELAY_MS) { /* spin */ }
    mLastUpdateMillis = nowms;
    for (u32 c4 = 0u; c4 < 4u; ++c4)
      mT6RingCorners[c4].update();
  }

  u32 T6RingOscillators::totalKWordsWritten() const {
    u64 tot = 0u;
    for (u32 c4 = 0u; c4 < 4u; ++c4)
      tot += mT6RingCorners[c4].mTotalWrites;
    return (u32) (tot / 1'000u);
  }

}
