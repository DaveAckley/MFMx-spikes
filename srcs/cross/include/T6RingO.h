#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U8C.h"
#include "S8C.h"
#include "Wrap8.h"
#include "ExtraConstants.h"
#include "Printf.h"
#include "EventWindow.h"
#include "CornerState.h"

namespace MFM {

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

  inline volatile u32 * funcGetNRIAddress(u32 noc, u32 nri, u32 byteOffset) {
    return (volatile u32*) (funcGetNRIBaseAddress(noc, nri) + byteOffset);
  }

  inline u32 funcReadNRIAddress(u32 noc, u32 nri, u32 byteOffset) {
    return *funcGetNRIAddress(noc,nri,byteOffset);
  }

  extern void funcWriteNRIAddress(u32 noc, u32 nri, u32 byteOffset, u32 value) ;

  struct NRI3 {
    u32 mSourceL1;
    u32 mDestL1;
    u32 mWordCount;
    U8C mSourceCoord0;
    U8C mDestCoord0;
    u8 mNoC;

    bool initiateWrite() ;

  private:
    u32 getNIUBaseAddress() const { return funcGetNIUBaseAddress(mNoC); }
    volatile u32 * getNIUAddress(u32 byteoffset) const { return funcGetNIUAddress(mNoC, byteoffset); }

    u32 readNIUAddress(u32 byteoffset) const { return *funcGetNIUAddress(mNoC, byteoffset); }
    void writeNIUAddress(u32 byteoffset, u32 value) const {
      funcWriteNIUAddress(mNoC, byteoffset, value);
    }

    //// NRI LEVEL
    u32 getNRIBaseAddress() const { return funcGetNRIBaseAddress(mNoC,3); }
    volatile u32 * getNRIAddress(u32 byteOffset) const {
      return funcGetNRIAddress(mNoC,3,byteOffset);
    }

    u32 readNRIAddress(u32 byteOffset) const { return funcReadNRIAddress(mNoC,3,byteOffset); }
    void writeNRIAddress(u32 byteOffset, u32 value) const {
      funcWriteNRIAddress(mNoC, 3, byteOffset, value);
    }

    bool isNRIBusy() const { return funcReadNRIAddress(mNoC,3,NRI_NOC_CMD_CTRL) & 1; }
    void waitTilNRIClear() const ;

  };

  struct CornerEW {
    CarSig mHeader;
    EventWindow mEW;
    CarSig mWaister;  // 16 <= offsetof(mFooter) - offsetof(mWaister) < 32
    CornerState mEWTag;
    S8C mEWOrigin; //< relative to ring corner as (0,0) with E:x+, S:y+
    u8 mEWCommand;
    u8 mSitesClaimed;
    CarSig mFooter;

    void init() ;
    u32 load(S8C center, Corner4 c4) ; //< t6Grid -> mEW
    u32 store(S8C center, Corner4 c4) ; //< mEW -> t6Grid
    u32 loadStore(Corner4 c4, bool doLoad) ;
    bool isComplete() const ;
  };

  /** A T6RingCorner manages one corner of a T6 generalized
      distributed ring oscillator.
   */
  struct T6RingCorner {

    /// Static NoC configuration
    u8 mNoC;                    // which NoC to use for xmit
    u8 mNRI;                    // which NRI to use on mNoC
    U8C mSourceCoord0;          // our coord always in mNoC0 terms
    U8C mDestCoord0;            // their coord always in mNoC0 terms

    /// Static Ring configuration
    bool mRingExists;           // true if all ngb ring corners exist
    Corner4 mRingCorner;        // which corner of a ring this represents
    u32 mDestAddress;           // MMIO address we write to in downstream
    u32 mSourceAddress;         // MMIO address upstream writes to in us

    /// Dynamic Ring-level state
    CornerState mLastRcvd;      // Most recently received upstream state
    CornerState mLastSent;      // Most recently sent downstream state
    u32 mTimesActive;
    u8 mResetPhase;
    u16 mAnchorCounter;
    u16 mActiveCounter;
    u64 mTotalWrites;

    NRI3 mShipEWConfig;

    void createEW();            //< populate our EW given we are active
    void sendEW(CornerState cs); //< send our EW downstream on tag cs

    char * c4Info() const ;

    void init(Corner4 c4) ;

    bool configureSelf(Corner4 c4) ;
    void configureNoC() ;

    u32 readUpstream() const ;

    void propagateDiff() {
      if (mLastRcvd.mWord != mLastSent.mWord)
        doPropagate(mLastRcvd);
    }
    void doPropagate(CornerState cs) ;
    bool doAnchorReset() ;
    void enterActive() ;
    void exitActive() ;
    void doActive() ;
    void doPassive() ;

    void update() ;
    
    //// NIU LEVEL
    u32 getNIUBaseAddress() const { return mNoC == 0u ? NIU_BASE_NOC0 : NIU_BASE_NOC1; }
    volatile u32 * getNIUAddress(u32 byteoffset) const { return (volatile u32*) (getNIUBaseAddress() + byteoffset); }

    u32 readNIUAddress(u32 byteoffset) const { return *getNIUAddress(byteoffset); }
    void writeNIUAddress(u32 byteoffset, u32 value) const ;

    //// NRI LEVEL
    u32 getNRIBaseAddress() const { return getNIUBaseAddress() + mNRI * NOC_CMD_BUF_OFFSET; }
    volatile u32 * getNRIAddress(u32 byteOffset) const { return (volatile u32*) (getNRIBaseAddress() + byteOffset); }

    u32 readNRIAddress(u32 byteOffset) const { return *getNRIAddress(byteOffset); }
    void writeNRIAddress(u32 byteOffset, u32 value) const ;

    void waitTilNRIClear() const ;

    void reportRegisterConfiguration(Printer & prt) const ;
    void sendDownstream(u32 val) ; //< write word to NoC and count it
  };

  struct T6RingOscillators {
    T6RingCorner mT6RingCorners[4]; // [Corner4]
    static constexpr u32 GATE_DELAY_MS = 3;
    u32 mLastUpdateMillis;

    void init() ;
    void update() ;
    u32 totalKWordsWritten() const ;
  };
}
