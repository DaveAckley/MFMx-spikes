#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U8C.h"
#include "S8C.h"
#include "Wrap8.h"
#include "ExtraConstants.h"
#include "Printf.h"
#include "EventWindow.h"
#include "CornerState.h"
#include "NRIUtils.h"

namespace MFM {

  struct CornerEW {
    CarSig mHeader;
    EventWindow mEW;
    CarSig mWaister;  // 16 <= offsetof(mFooter) - offsetof(mWaister) < 32
    CornerState mEWTag;
    S8C mEWOriginCC; //< ew origin in corner coords
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

    //NRI3 mShipEWConfig;

    void createEW(CornerState tag); //< populate our EW given we are active
    void sendEW();                  //< send our EW downstream

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
