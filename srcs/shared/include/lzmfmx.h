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
  
  using LZBuf = RingBuffer<u8,6>; // 5 bits minimum so (deprecated->) hasRoomForNMore(sizeof(AtomReport)) is safe

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
