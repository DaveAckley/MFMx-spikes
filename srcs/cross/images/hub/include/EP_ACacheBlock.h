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
    u16 mExpectLen;             //< payload length our own last accepted byte left
    u8 mLenJumpsThisCar;        //< length found moved under us, this car (log cap)
    u32 mLenJumps;              //< times the length was not where we left it (.zl)
    u32 mRecvMs[8];             //< millisElapsed() at each car's last recvTC
    u32 mRecvN[8];              //< recvTC calls per car
    u32 mRecvCurrent;           //< recvTC calls on the car we are filling (should be 0)
    u32 mShadowBad;             //< cars whose contents differed at seal time
    u32 mShadowStale;           //< seal reads of the shadow sums that a fence changed
    u8 mShadowCar[ACacheBlockPayload::ACBP_DATA_SIZE]; //< the bytes the sink accepted, as meant
    void explainShadow(const ACacheBlockPayload & pay, u16 seq) ; //< T0, at seal: log the diffs

    /* Host-directed refresh (ACBHostFeedback / REFRESH slots in
       PT_ACacheBlock.h): sites the host asked us to re-report.  The NC hart
       latches them in recvTC (under mLock); T0 queues each on the change
       list (pushFrontC), so the next report of the site carries its current
       atom and change stamp. */
    static constexpr u32 REFRESH_QLEN = 16u;
    U8C mRefreshC[REFRESH_QLEN];
    u8 mRefreshN;               //< sites waiting
    u32 mRefreshAsked;          //< sites latched (valid), for the .hr mark
    u32 mRefreshFull;           //< valid sites dropped, queue full

    void init() ;
    void latchHostFeedback(const ACacheBlockPayload & pay) ; // NC, BEFORE init()
    bool takeRefreshSite(U8C & c) ; //< T0
    bool closeIfData() ;        //< T0: mark the car being filled closable
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
    /* One report frame (one ACB car at most) per this many ms: the cap on
       the hub's report rate.  Was 50 (20 cars/s); 25 is an experiment to
       see whether the drain rate follows it.  Build with
       -DACB_BOGOMS_PER_FRAME=50u to go back. */
#ifndef ACB_BOGOMS_PER_FRAME
#define ACB_BOGOMS_PER_FRAME 25u
#endif
    static constexpr u32 BOGOMS_PER_FRAME = ACB_BOGOMS_PER_FRAME;
    
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
    u32 mEpochSent;             // epoch of the last marker in the stream,
                                //   or NO_EPOCH_SENT (stream start/restart)
    static constexpr u32 NO_EPOCH_SENT = 0x10000u;
    u32 mEpochMarkers;          // markers sent, for the .em mark

    /* IDLE-TIME REFRESH: when the change list is empty in the middle of a
       frame, report up to REFRESH_PER_FRAME randomly chosen SELF sites
       anyway (their current atoms), so a stale pixel on the host display
       (a "turd" nothing ever repainted) is repainted from the hub's truth,
       gradually.  Cost only idle time; 0 turns it off.  Self sites only, so
       the owner has the last word (never the cache ring). */
#ifndef ACB_REFRESH_PER_FRAME
#define ACB_REFRESH_PER_FRAME 0u  // 0 while hunting turds (refresh erases the
                                  //   evidence before `who`); normal value 20u
#endif
    static constexpr u32 REFRESH_PER_FRAME = ACB_REFRESH_PER_FRAME;
    u32 mRefreshLeft;           // refresh reports still allowed this frame
    u32 mRefreshSent;           // reports sent as refresh, for the .rf mark
    u32 mRefreshQueued;         // host-asked sites queued, for the .hr mark

    /* IDLE CLOSE: a car ships only when full or marked closable, and the
       per-frame mark was commented out, so when the change list ran dry the
       tail of the stream (up to a car's worth of reports) sat in the hub
       until traffic resumed, and with no car shipped the host had nothing
       to return a request in.  With this on, a frame that finds the change
       list empty marks a car that has any bytes closable, so it ships.
       Under load nothing changes (the list is not empty).  Build with
       -DACB_IDLE_CLOSE=0u to go back. */
#ifndef ACB_IDLE_CLOSE
#define ACB_IDLE_CLOSE 1u
#endif
    u32 mIdleCloses;            // cars marked closable while idle, for the .ic mark

    U8C nextRefreshSite() ;

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
