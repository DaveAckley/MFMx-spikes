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

    /* Stream restart (see ACBP_FLAG_RESTART_REQ in PT_ACacheBlock.h).  The
       host asks in the cars it returns (latched by the NC hart in recvTC
       before the payload is wiped); T0 stops feeding the compressor at a
       record boundary and sets mDraining; T1's byte source ends the stream
       (EOF) once the ring is empty, the encoder flushes, T1 marks the last car
       EOF|CLOSABLE and the next SOF, waits for T0 to ship it, resets the
       encoder and clears mDraining; T0 then resumes.  All under mLock except
       the mDraining test T1's source makes per byte. */
    u8 mHostReq;                //< latest request flag seen in a returned car
    u8 mHostReqEpoch;           //< and its epoch bit
    u8 mHandledEpoch;           //< epoch of the last restart we completed
    u8 mDraining;               //< T0 -> T1: no more input, end the stream
    u8 mPendingSOF;             //< T1 -> T0: stamp SOF on the next car
    u8 mSOFEpoch;               //< the epoch that SOF answers
    u16 mHostSpin, mHostSeq, mHostConsec;  //< the host's feedback, last request
    u32 mRestartsDone;
    u16 mShadowA, mShadowB;     //< Fletcher sums of the bytes the sink accepted
    u16 mShadowN;               //< bytes folded into the shadow sum for this car
    u16 mShadowEarly;           //< shadow sum (b<<8|a) as it stood after 16 folds
    u32 mShadowBad;             //< cars whose contents differed at seal time
    u32 mShadowStale;           //< seal reads of the shadow sums that a fence changed
    u8 mShadowCar[ACacheBlockPayload::ACBP_DATA_SIZE]; //< the bytes the sink accepted, as meant
    void explainShadow(const ACacheBlockPayload & pay, u16 seq) ; //< T0, at seal: log the diffs

    void init() ;
    void latchHostFeedback(const ACacheBlockPayload & pay) ; // NC, BEFORE init()
    bool restartWanted() ;      //< T0: a request we have not answered
    void beginDrain() ;         //< T0
    bool isDraining() const ;   //< T1's source (no lock) and T0
    void finishStream() ;       //< T1: the encoder has flushed; end the car
    bool sofPending() ;         //< T1 waits until T0 has shipped the car
    void streamRestarted() ;    //< T1: encoder reset; T0 may resume
    void logRestart(const char what, u32 spinner) ;
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
    u8 mRestartPhase;           // 0 none, 1 flush the record in flight, 2 drain
    u16 mPktSeq;                // next packet's sequence number (trailer)

    bool restartStep(HostBlock & hb) ;  // true: restarting, no frame work now

    void init(ACacheBlockL1Control & acbl1, DLGridList & dll1) ;
    int step(HostBlock & hb) ;
    void updateCars(HostBlock & hb) ;
    void tryToSendFrame(HostBlock & hb);
    ACacheBlock & getCurrentACBOrDie() ;
  };

  extern T6EPL1Data<ACacheBlockStg,1u> theACacheBlockL1Data;
  extern ACacheBlockL1Control theACacheBlockL1Control;
  extern ACacheBlockEP myACacheBlockEPNC;
}
