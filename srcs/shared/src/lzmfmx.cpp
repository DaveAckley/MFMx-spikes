/* Dave's first attempt at a custom LZ(W|SS|??)
   compressor for such as atoms and coords

   --Mon Jun 15 01:11:21 2026 
   Redo for funcptr IO instead of classes

   --Sun Jun  7 16:22:27 2026
   First 'release' into the mfmx codebase

   --Fri Jun  5 12:26:34 2026
   Created from Matt Seabrook's (info@mattseabrook.net) 2026 public
   domain lzssx.c, in turn based on Haruhiko Okumura's 1989
   Ur-Implementation
*/

#include "lzmfmx.h"
#include "Fail.h"
#include "XUtils.h"             // for memset_s
#include "Debug.h"              // for LOG* etc

#if 0
#undef FAIL
#define FAIL(msg) do { fprintf(stderr,"%s:%d:FAIL: %s\n",__FILE__,__LINE__,""#msg); exit(9); } while(0)
#endif

namespace MFM {

#ifdef HOST
  static void printTree(lzmfmx& lz, u8 tree) {
    printf("SDOFDSIO\n");
  }
#endif

  void lzmfmx::init(ByteSourceFuncPtr ucompin, void * inctxt, ByteSinkFuncPtr compout, void * outctxt) {
    memset_s(this,'\0',sizeof(*this));

    MFM_API_ASSERT_NONNULL(ucompin);
    MFM_API_ASSERT_NONNULL(compout);
    mInPtr = ucompin;
    mInCtxt = inctxt;
    mOutPtr = compout;
    mOutCtxt = outctxt;

    for (u32 i = RING_SIZE + 1; i <= RING_SIZE + 256; ++i) mRc[i] = NIL;
    for (u32 i = 0; i <= RING_SIZE; ++i) mPar[i] = NIL;
  }

  bool lzmfmx::inputIsEOF() {
    s32 v = mInPtr(true,mInCtxt);
    //    SNAP(100,LOGXTAG(iIEOF,v));
    return v < 0;
  }

  s32 lzmfmx::getNextByteBlocking() {
    s32 v;
    u32 spin = 0;
    while (true) {
      v = mInPtr(false,mInCtxt);
      if (v >= 0) break;
      waitALittle();
      if ((++spin % 1'000'000)==0) {
        LOGPTAG(lzmgNBB,spin);
        LOGPX(mBytesIn);
        LOGPX(mBytesOut);
      }
    }
    ++mBytesIn;
    return (s32) v;
  }

  s32 lzmfmx::putNextByteBlocking(u8 byte) {
    while (!mOutPtr(byte,mOutCtxt)) {
      SNAP(100,LOGXTAG(BLOK,(u32)byte));
    }
    if ((++mBytesOut % 0x1f)==0) HBPTAG(pNBB,mBytesOut);
    return (s32) byte;
  }

  void lzmfmx::insertNode(u32 r) {
    if (r >= sizeof(mRing)) FAIL(ILLEGAL_ARGUMENT);
    u8 *key = &mRing[r];
    u32 p = RING_SIZE + 1 + key[0];
    mRc[r] = mLc[r] = NIL;
    mMlen = 0;
    s32 cmp = 1;
    for (;;) {
      u16 *branch = cmp >= 0 ? &mRc[p] : &mLc[p];
      if (*branch != NIL) { p = *branch; }
      else {
        *branch = r;
        mPar[r] = p;
        return;
      }

      u32 i = 1;
      while (i < MAX_MATCH && key[i] == mRing[p + i]) ++i;
      if (i > mMlen) {
        mMpos = p;
        mMlen = i;
        if (i >= MAX_MATCH) break;
      }
      cmp = key[i] - mRing[p + i];
    }
    mPar[r] = mPar[p];
    mLc[r] = mLc[p];
    mRc[r] = mRc[p];
    mPar[mLc[p]] = mPar[mRc[p]] = r;
    if (mRc[mPar[p]] == p) 
      mRc[mPar[p]] = r;
    else 
      mLc[mPar[p]] = r;
    mPar[p] = NIL;
  }

  void lzmfmx::deleteNode(u32 p) {
    if (mPar[p] == NIL) return;
    u32 q;
    if (mRc[p] == NIL) q = mLc[p];
    else if (mLc[p] == NIL) q = mRc[p];
    else {
      q = mLc[p];
      if (mRc[q] != NIL) {
        while (mRc[q] != NIL) q = mRc[q];
        mRc[mPar[q]] = mLc[q];
        mPar[mLc[q]] = mPar[q];
        mLc[q] = mLc[p];
        mPar[mLc[p]] = q;
      }
      mRc[q] = mRc[p];
      mPar[mRc[p]] = q;
    }
    mPar[q] = mPar[p];
    if (mRc[mPar[p]] == p)
      mRc[mPar[p]] = q;
    else
      mLc[mPar[p]] = q;
    mPar[p] = NIL;
  }

  void lzmfmx::compressForever() {
    HBMARK;
    LOGMARK;
    while (true) {
      encode();
      SNAP(100,LOGPTAG(encret?,this)); // unexpected since ubs has no eof..
    }
  }

  bool lzmfmx::encode() {
    HBPTAG(lzmenc,"ARO");
    u8 code[17], flags = 0, mask = 1;
    u32 cptr = 1, sid = 0, r = RING_SIZE - MAX_MATCH, n = 0;

    // Prime lookahead buffer
    while (n < MAX_MATCH && !inputIsEOF()) {
      s32 sb = getNextByteBlocking();
      if (sb < 0) LOGPTAG(lzenc,sb);
      else mRing[r + n++] = (u8) sb;
    }

    HBPTAG(lzmenc,"PRIMD");

    for (u32 i = 1; i <= MAX_MATCH; ++i)
      insertNode(r - i);
    insertNode(r);

    HBPTAG(lzmencN,n);
    while (n > 0) {
      if (true) {
        static u32 spin = 0;
        if ((spin++ & 0xff) == 0)
          HBPTAG(lzmenc,n);
      }

      u32 ml = mMlen > n ? n : mMlen;
      if (ml <= MIN_MATCH) {
        LOGXTAG(lzlit,(u32) mRing[r]);
        ml = 1; flags |= mask; code[cptr++] = mRing[r];
      } else {
        code[cptr++] = mMpos & 0xFF;
        code[cptr++] = ((mMpos >> 4) & 0xF0) | (ml - MIN_MATCH - 1);
        LOGXTAG(lzref,(u32) (((mMpos&0xff)<<16)|ml));
      }
    
      if (!(mask <<= 1)) {
        LOGXTAG(lzflg,(u32) flags);
        code[0] = flags;
        for (u32 i = 0; i < cptr; ++i)
          putNextByteBlocking(code[i]); // abstract: pack packets in here too
        LOGXTAG(lzwrt,(u32) cptr);
        HBPTAG(lz2wrt,(u32) cptr);
        flags = 0;
        mask = 1;
        cptr = 1;
      }

      // Slide window by ml positions
      for (u32 i = 0; i < ml; ++i) {
        deleteNode(sid);
        if (!inputIsEOF()) {
          u8 c = getNextByteBlocking();
          mRing[sid] = c;
          if (sid < MAX_MATCH - 1) mRing[sid + RING_SIZE] = c;
        } else {
          --n;
        }
        if (++sid >= RING_SIZE) sid = 0; // avoid  '& (N - 1)'
        if (++r >= RING_SIZE) r = 0;     // so power of 2 unneeded
        if (n > 0) insertNode(r);
      }
    }

    // Flush tail if any
    if (cptr > 1) {
      code[0] = flags;
      for (u32 i = 0; i < cptr; ++i)
        putNextByteBlocking(code[i]);
    }
    return true;
  }

#if 0
  bool lzmfmx::encode() {
    while (true) {
      s32 s = getNextByteBlocking();
      if (s >= 0) putNextByteBlocking((u8) s);
      else break;
    }
    return true;
  }
#endif

  bool lzmfmx::decode() {
    memset_s(mRing, '\0', RING_SIZE - MAX_MATCH);
    uint32_t r = RING_SIZE - MAX_MATCH;
    while (!inputIsEOF()) {
      u32 flags = getNextByteBlocking() | 0xFF00;
      for (; (flags & 0x100) && !inputIsEOF(); flags >>= 1) {
        if (flags & 1) {
          u8 c = getNextByteBlocking();
          //buf_grow(&out, 1);
          putNextByteBlocking(c);
          mRing[r] = c;
          if (++r >= RING_SIZE) r = 0;
        } else {
          if (inputIsEOF()) break;
          u32 lo = getNextByteBlocking();
          if (inputIsEOF()) break;
          u32 hi = getNextByteBlocking();
          u32 p = lo | ((hi & 0xF0) << 4);
          u32 ml = (hi & 0x0F) + MIN_MATCH + 1;
          //buf_grow(&out, ml);
          for (u32 k = 0; k < ml; ++k) {
            u32 idx = p + k;
            if (idx >= RING_SIZE) idx -= RING_SIZE;
            u8 c = mRing[idx];
            putNextByteBlocking(c);
            mRing[r] = c;
            if (++r >= RING_SIZE) r = 0;
          }
        }
      }
    }
    return true;
  }

#ifdef HOST
  static void fp(u32 indent, u8 byte) {
    for (u32 i = 0; i < indent; ++i)
      fprintf(stderr, " ");
    fprintf(stderr, "%02x ",byte);
  }
  void lzmfmx::printBuffer() {
    fprintf(stderr,"BONDNFGO");
    for (u32 r = 0; r < RING_SIZE; ++r) {
      if ((r % 25) == 0)
        fprintf(stderr,"\n%4u. ",r);
      u8* key = &mRing[r];
      //u32 p = RING_SIZE + 1 + key[0];
      fp(0,key[0]);
    }
    fprintf(stderr,"\n");
  }
  void lzmfmx::printTrees() {
    for (u32 r = 0; r < RING_SIZE; ++r) {
      if (mPar[r] != NIL) {
        fprintf(stderr,"\n%3d. ",r);
        printTree(r);
      }
    }
  }
  void lzmfmx::printTree(u32 r,u32 indent) {
    u8* key = &mRing[r];
    if (mRc[r] != NIL) printTree(mRc[r],indent + 2);
    nli(indent);
    fprintf(stderr,"0x%02x",key[0]);
    if (mLc[r] != NIL) printTree(mLc[r],indent + 2);
  }
#endif //HOST
}

