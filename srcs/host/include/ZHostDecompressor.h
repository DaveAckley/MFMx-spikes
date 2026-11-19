#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "UxC.h" // for U16C
#include "lzmfmx.h"
#include "AtomicLock.h"
#include "PT_ACacheBlock.h"  // for ACacheBlock
#include "AtomReport.h"      // for AtomReportIO

#include <thread> // for thread
#include <memory> // for unique_ptr
#include <atomic> // for atomic
#include <mutex>  // for mutex
#include <vector> // for vector

namespace MFM {

  /* Stream health of one hub's compressed atom-report stream (see
     ZHostDecompressor::noteReport).  The stream is an LZ stream with no
     resync marker, so one lost or corrupted byte garbles every later
     report; the host sees that as reports whose spin counters do not
     match.  Copied out by ZHostDecompressor::snapshotHealth(). */
  struct ZHDHealth {
    u32 mChip = 0u;
    u32 mTLBI = 0u;
    u64 mReports = 0u;         //< good reports so far
    u64 mInvalid = 0u;         //< reports that failed the spin check
    u64 mBadAtoms = 0u;        //< intact reports whose atom failed parity
    u32 mConsecInvalid = 0u;   //< failed in a row, right now
    u32 mDesyncs = 0u;         //< times the stream was declared desynced
    bool mDesynced = false;    //< declared desynced and not recovered
    u64 mDesyncAtReport = 0u;  //< good reports before the first bad one
    u64 mDesyncAtCBytes = 0u;  //< compressed bytes consumed by then
    u32 mDesyncAtPacket = 0u;  //< compressed packets taken by then
    u32 mDesyncKind = 0u;      //< 1 = whole reports skipped, 2 = garbage
    u32 mRestarts = 0u;        //< stream restarts completed (the hub answered)
    bool mRestartPending = false; //< asked for a restart, no answer yet
  };

  struct ZHostDecompressor {
    static u32 mMade;
    /* Consecutive failed spin checks before the stream is declared
       desynced.  One is already proof (spin only advances on a good
       report); the margin just keeps the declaration out of a log line
       per odd byte. */
    static constexpr u32 DESYNC_THRESHOLD = 20u;
    static constexpr u32 PKT_HISTORY = 16u;   //< packets remembered
    static constexpr u32 OUT_HISTORY = 48u;   //< decoded bytes remembered

    /* Health of every live decompressor (one per hub), for the pybind
       zhdSnapshot() and the 'zhd' mfmxctl command. */
    static std::vector<ZHDHealth> snapshotHealth() ;

    /* Ask the hub to restart its stream when it desyncs (default on).  Off:
       desyncs are still detected and logged, nothing is requested. */
    static void setRestartEnabled(bool on) ;
    static bool restartEnabled() ;

    /* TEST: make the hub's decoder drop one compressed byte, as a lost byte
       would (mfmxctl 'zhd break TAG').  True if that hub has a decompressor. */
    static bool injectDrop(u32 chip, u32 tlbi) ;
    ZHostDecompressor() ;
    ~ZHostDecompressor() ;
    void init(const u32 tlbi, const u32 chipnum) ;

    /** Ask the decompressor thread to quit (it notices at its next source
        or sink byte and unwinds) and join it.  Safe to call when no thread
        is running, and more than once. */
    void stopThread() ;
    lzmfmx mLZ;

    BHTag mBHTag;
    u32 mTLBI;
    u32 mChipNum;
    std::unique_ptr<std::thread> mDecompressorThreadPtr;
    std::atomic<bool> mQuitDecompressorThread;
    AtomicLock mDecompressorThreadMutex;

    s32 sourceByte(bool canread) ;
    bool sinkByte(const u8 byte) ;

    typedef std::pair<u32,ACacheBlock*> CarACB;
    typedef RingBuffer<CarACB,3> PRB;
    PRB mACBI, mACBO;
    u8 mARSpinner = 0;
    AtomReportIO mHARIO;
    CarACB mCurrentCarACB;
    u32 mCurrentBytesRead = 0;
    u64 mAtomReportsReceived = 0;  //< low 16 bits are the expected spin
    u64 mCompressedBytesIn = 0;
    u64 mUncompressedBytesOut = 0;

    //// Stream health (decompressor thread writes; snapshotHealth() reads)
    struct PacketRec {
      u32 mSeq = 0u;           //< 1 for the first packet taken
      u32 mCar = 0u;           //< car number in the hub's ACB ring
      u32 mFlagsAndLen = 0u;   //< as received
      u64 mStartOffset = 0u;   //< compressed bytes consumed before it
      u8 mHead[4] = {0,0,0,0};
      u8 mTail[4] = {0,0,0,0};
    };
    PacketRec mPktHist[PKT_HISTORY];
    u32 mPacketsIn = 0u;
    u8 mOutHist[OUT_HISTORY];  //< ring of the last decoded bytes
    u32 mOutHistPos = 0u;
    u32 mConsecInvalid = 0u;
    u32 mDesyncs = 0u;
    u64 mInvalidTotal = 0u;
    u64 mBadAtoms = 0u;        //< intact reports with a bad atom (not drawn)
    u64 mDesyncAtReport = 0u;
    u64 mDesyncAtCBytes = 0u;
    u32 mDesyncAtPacket = 0u;
    u32 mDesyncKind = 0u;
    std::atomic<bool> mDesynced{false};
    mutable std::mutex mHealthLock;  //< guards the fields above vs snapshot
    bool mRegistered = false;

    //// Stream restart (ACBP_FLAG_RESTART_REQ in PT_ACacheBlock.h)
    std::atomic<bool> mRestartRequested{false};  //< until the matching SOF
    std::atomic<u32> mHostEpoch{0u};             //< toggles per request
    std::atomic<u32> mRestarts{0u};
    std::atomic<bool> mInjectDrop{false};  //< TEST: drop the next byte
    bool mSpinSync = false;  //< adopt the hub's spin from the next good record

    /* Stamp a car we are returning: the restart request (if any) in its flags
       and where our decoder stands in its payload; the car stays empty. */
    void stampFeedback(ACacheBlockPayload & pay) const ;
    void requestRestart() ;
    bool isRestartSOF(const ACacheBlockPayload & pay) const ;
    void restartDecoder() ;

    void notePacket(u32 car, const ACacheBlockPayload & pay) ;
    void noteGoodReport() ;
    void noteBadReport(const AtomReport & ar, u16 expected) ;
    void logDesyncDetail(const char * what, const AtomReport & ar, u16 expected) ;
    ZHDHealth health() const ;
  };
}




  
