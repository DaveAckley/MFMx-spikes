#pragma once     /* -*- C++ -*- */
#include "itype.h"
#include "RingBuffer.h"
#include <cstdio>
#include "Fail.h"
#include "Debug.h"

#ifndef BUILD_HOST
#include "FastLocal.h"
inline void waitALittle() { MFM::sleepCycles(500); }
#else
inline void waitALittle() { }
#endif

namespace MFM {
  using ByteSourceFuncPtr = s32 (*)(bool canread, void * ctxt); // >=0 byte, -1 eof, -2 blocked
  using ByteSinkFuncPtr = bool (*)(const u8, void * ctxt); // true: wrote, false: blocked, error, or eof
  
  using LZBuf = RingBuffer<u8,6>; // 5 bits minimum so hasRoomForNMore(sizeof(AtomReport)) is safe

#if 0

  struct ByteSource {
    LZBuf * mBufL1;
    ByteSource() : mBufL1(0) { }

    void init(LZBuf & l1src) {
      mBufL1 = & l1src; 
    }

    /// APPY
    s32 getNextByteBlocking() {
      if (!mBufL1) return -2;
      do {
        u8 ch;
        if (mBufL1->remove(ch))
          return (s32) ch;
        waitALittle();          // don't pound L1 toooo hard
      } while (true);
    }

    bool isEOF() {
      return false;
    }
  };

  struct ACacheBlockL1Control; // FORWARD

  struct ByteSink {
    ACacheBlockL1Control * mACBL1;
    ByteSink() : mACBL1(0) { }

    void init(ACacheBlockL1Control & acbl1) {
      memset_s(this,'\0',sizeof(*this));
      mACBL1 = &acbl1;
    }

    /// APPY
    s32 putNextByteBlocking(u8 byte) {
      MFM_API_ASSERT_NONNULL(mACBL1);
      ACacheBlockL1Control & acbl1 = *mACBL1;

      do {
        {                       //// TAKE L1 LOCK
          AtomicScopeLock guard(acbl1.mLock);
          if (acbl1.mCurrentACacheBlock) {
            ACacheBlock & acb = *acbl1.mCurrentACacheBlock;
            ACacheBlockPayload & pay = acb.payload();
            if (pay.addByte(byte)) return (s32) byte;
          }            
        } 

        waitALittle();          // don't pound L1 toooo hard
        {
          u32 spin = 0;
          if ((++spin & 0xfff) == 0)
            LOGPTAG(pNBB,byte);
        }
      } while (true);
    }

    bool isNextByteSpaceAvailable() const {
      MFM_API_ASSERT_NONNULL(mACBL1);
      ACacheBlockL1Control & acbl1 = *mACBL1;

      AtomicScopeLock guard(acbl1.mLock); // TAKE LOCK
      return acbl1.mCurrentACacheBlock &&
        acbl1.mCurrentACacheBlock->payload().getBytesRemaining() > 0;
    }
  };

  struct LZByteSink {
    LZBuf * mBufL1;
    LZByteSink() : mBufL1(0) { }

    void init(LZBuf & l1snk) {
      mBufL1 = &l1snk; 
    }

    /// APPY
    s32 putNextByteBlocking(u8 byte) {
      if (!mBufL1) return -2;
      do {
        if (mBufL1->add(byte)) return (s32) byte;
        waitALittle();          // don't pound L1 toooo hard
        {
          u32 spin = 0;
          if ((++spin & 0xfff) == 0)
            LOGPTAG(pNBB,byte);
        }
      } while (true);
    }
    bool isNextByteSpaceAvailable() const {
      MFM_API_ASSERT_NONNULL(mBufL1);
      return !mBufL1->isFull();
    }
  };

  struct FileByteSource {
  //struct ByteSource {
    FILE * mFile;
    bool mHaveNextByte;
    s32 mNextByte;

    /// APPY
    s32 getNextByteBlocking() {
      if (!mFile) return -2;
      if (mHaveNextByte) {
        mHaveNextByte = false;
        return mNextByte;
      }
      return fgetc(mFile);
    }

    bool isEOF() {
      if (!mHaveNextByte) {
        mNextByte = fgetc(mFile);
        mHaveNextByte = true;
      }
      return mNextByte < 0;
    }
  };

  struct FileByteSink {
  //  struct ByteSink {
    FILE * mFile;

    /// APPY
    s32 putNextByteBlocking(u8 byte) {
      if (!mFile) return -2;
      fputc(byte,mFile);
      return byte;
    }
    bool isNextByteSpaceAvailable() const {
      return true;
    }
  };
#endif

  struct lzmfmx {
    static constexpr u32 RING_SIZE = 348; // Ring buffer size in bytes (aka N (not necessarily power of 2))
    static constexpr u32 MAX_MATCH = 18; // Maximum match length (aka F, lookahead size)
    static constexpr u32 MIN_MATCH = 2; // Minimum match for encoding (aka THR, encode threshold)
    static constexpr u32 NIL = RING_SIZE; // Null node pointer for binary search tree

    static_assert(RING_SIZE == 348 && MAX_MATCH == 18 && MIN_MATCH == 2, "mfmx params");

    void init(ByteSourceFuncPtr ucompin, void * inctxt, ByteSinkFuncPtr compout, void * outctxt) ;

    bool inputIsEOF() ;

    void compressForever() ;

    bool encode() ;
    bool decode() ;

#ifdef HOST
    void printBuffer() ;
    void printTrees() ;
    void printTree(u32 i, u32 indent = 0) ;
    void nli(u32 amt) {
      fprintf(stderr,"\n");
      for (u32 i = 0; i < amt; ++i)
        fprintf(stderr," ");
    }
#endif

  private:
    s32 getNextByteBlocking();
    s32 putNextByteBlocking(u8);

    void insertNode(u32 r);
    void deleteNode(u32 p);

    ByteSourceFuncPtr mInPtr;
    void * mInCtxt;
    ByteSinkFuncPtr mOutPtr;
    void * mOutCtxt;

    u32 mBytesIn, mBytesOut;

    u8 mRing[RING_SIZE + MAX_MATCH - 1];
    u16 mLc[RING_SIZE + 1], mRc[RING_SIZE + 257], mPar[RING_SIZE + 1];

    u16 mMpos;                  // match pos(?)
    u16 mMlen;                  // match len
  };
}
