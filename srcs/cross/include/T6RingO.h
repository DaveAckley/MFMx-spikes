#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U8C.h"
#include "Wrap8.h"
#include "ExtraConstants.h"

namespace MFM {


  /** A T6RingCorner manages one corner of a T6 generalized
      distributed ring oscillator.
   */
  struct T6RingCorner {

    /// Static NoC configuration
    u8 mNoC;                    // which NoC to use for xmit
    u8 mNRI;                    // which NRI to use for mNoC
    U8C mSourceCoord;           // our coord in mNoC terms
    U8C mDestCoord;             // their coord in mNoC terms

    /// Static Ring configuration
    bool mRingExists;           // true if all ngb ring corners exist
    Corner4 mRingCorner;        // which corner of a ring this represents
    u32 mDestAddress;           // MMIO address we write to in downstream
    u32 mSourceAddress;         // MMIO address upstream writes to in us

    /// Dynamic Ring-level state
    CornerState mLastState;     // Our last view of this ring
    CornerState mNewState;      // Our new view of this ring
    u8 mResetPhase;
    u16 mAnchorCounter;
    u32 mTimesActive;

    char * c4Info() const ;

    void init(Corner4 c4) ;

    bool configureSelf(Corner4 c4) ;
    void configureNoC() ;

    u32 readUpstream() const ;

    void doPropagate() ;
    bool doAnchorReset(u32 newcs) ;
    void doActive() ;

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

    void sendDownstream(u32 val) const ;
  };

  struct T6RingOscillators {
    T6RingCorner mT6RingCorners[4]; // [Corner4]

    void init() ;
    void update() ;
  };
}
