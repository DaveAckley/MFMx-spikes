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

namespace MFM {

  struct ZHostDecompressor {
    static u32 mMade;
    ZHostDecompressor() ;
    void init(const u32 tlbi, const u32 chipnum) ;
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
    u8 mARSpinner;
    AtomReportIO mHARIO;
    CarACB mCurrentCarACB;
    u32 mCurrentBytesRead;
    u64 mAtomReportsReceived;
    u64 mCompressedBytesIn;
    u64 mUncompressedBytesOut;
  };
}




  
