#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "EP.h"
#include "T6EPs.h"
#include "PT_ACacheBlock.h"
#include "TCStorage.h"

namespace MFM {

  static constexpr u32 HACB_BLOCKS = 1u;
  struct ACacheBlockEP : public T6EP<ACacheBlockEP,ACacheBlockStg,HACB_BLOCKS,HARTNUM_NC> {
    using Super = T6EP<ACacheBlockEP,ACacheBlockStg,HACB_BLOCKS,HARTNUM_NC>;

    //// EP API
    const char * getName() const { return isIn() ? "ACBi" : "ACBo"; }
    u32 getCarSize() const { return sizeof(ACacheBlock); }
    ACacheBlock * getCarPtrIfAny(u8 carindex) const ;
    TCOpsData & getOpsData(u8 carindex) {
      HBASSERT_LT(carindex, CAR_COUNT);
      return mOpsDataStg[carindex];
    }
    bool recvTC(ACacheBlock & car, u8 carindex) ;

    void initACacheBlockEP(EndPointAddress srcEPA, bool isin, typename Super::L1Data & l1data) ;
  };

  struct ACacheBlockL1Control {
    using TheL1Data = ACacheBlockEP::Super::L1Data;

    AtomicLock mLock;
    ACacheBlock * mCurrentACacheBlock;
    u8 mCurrentCarIndex;        // valid whenever mCurrentACacheBlock != 0
    u32 mCompressedBytesPacked;
    u32 mBaseTicks;
    u32 mAReportsMissed;
    static constexpr u8 ACBL1_NC_INITTED = 0x01;
    static constexpr u8 ACBL1_T0_INITTED = 0x02;
    static constexpr u8 ACBL1_T1_INITTED = 0x04;
    u8 mFlags;

    void init() ;
    void setFlags(u8 flags) ;
    bool testFlags(u8 flags) const ;
    u8 getFlags() const { return mFlags; }

    // int step(HostBlock & hb) ;
    bool readyToClose() ;
    bool writeByteToCurrentACB(u8 byte) ;
    void setupNewCar(TheL1Data::CarIdxRB & crbi) ; // COMP-SIDE CALLER HOLDS LOCK & crbi IS NON-EMPTY
 private:
  };

  struct DLGridList;            // FORWARD
  struct ACacheBlockPrivateControl {
    using TheL1Data = ACacheBlockEP::Super::L1Data;

    enum State : u8 {
      UNINIT = 0u,            // unknown / illegal
      NEXT = 1u,              // waiting for next car to fill
      WAIT = 2u,              // waiting for end of frame time
      PACK = 3u,              // compressing and shipping ARs
    };
    
    const char * getStateName(State us) ;

    static constexpr u32 RUN_ON_HARTNUM = HARTNUM_T1;
    static constexpr u32 BOGOMS_PER_FRAME = 50u; // XXX 100u
    
    //    lzmfmx mARCompressor; -> FastT1
    AtomReportIO mARIO;
    ACacheBlockL1Control * mACBL1Control;
    DLGridList * mDLGridList;
    u32 mUncompressedBytesPacked;
    u32 mARsToSend;
    u32 mFramesSent;
    s32 mSlackMS;
    u32 mNextFrameBogoMS;
    State mState;
    u16 mARSpinner;

    void init(ACacheBlockL1Control & acbl1, DLGridList & dll1) ;
    int step(HostBlock & hb) ;
    int updateCars(HostBlock & hb) ;
    void tryToSendFrame(HostBlock & hb);
    ACacheBlock & getCurrentACBOrDie() ;
  };

  extern T6EPL1Data<ACacheBlockStg,1u> theACacheBlockL1Data;
  extern ACacheBlockL1Control theACacheBlockL1Control;
  extern ACacheBlockEP myACacheBlockEPNC;
}
