#pragma once     /* -*- C++ -*- */
#include "itype.h"
#include "RingBuffer.h"
#include <cstdio>

namespace MFM {
  //  struct MemByteSource {
  struct ByteSource {
    u8 * mData;
    u32 mLen;
    u32 mPos;

    /// APPY
    s32 getNextByteBlocking() {
      if (!mData) return -2;
      if (mPos >= mLen) return -1;
      return mData[mPos++];
    }

    bool isEOF() {
      return mData && mPos >= mLen;
    }
  };

  //  struct MemByteSink {
  struct ByteSink {
    u8 * mData;
    u32 mLen;
    u32 mPos;

    /// APPY
    s32 putNextByteBlocking(u8 byte) {
      if (!mData) return -2;
      if (mPos >= mLen) return -1;
      mData[mPos++] = byte;
      return 0;
    }
    bool isNextByteSpaceAvailable() const {
      return mData && mPos < mLen;
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

#if 0
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

    void init();
    bool encode(ByteSource & ubs, ByteSink & cbs) ;
    bool decode(ByteSource & cbs, ByteSink & ubs) ;

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
    void insertNode(u32 r);
    void deleteNode(u32 p);

    u8 mRing[RING_SIZE + MAX_MATCH - 1];
    u16 mLc[RING_SIZE + 1], mRc[RING_SIZE + 257], mPar[RING_SIZE + 1];

    u16 mMpos;                  // match pos(?)
    u16 mMlen;                  // match len
  };
}
