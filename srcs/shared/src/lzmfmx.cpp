/* Dave's first attempt at a custom LZ(W|SS|??)
   compressor for such as atoms and coords

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

  void lzmfmx::init() {
    memset_s(this,'\0',sizeof(*this));
    for (u32 i = RING_SIZE + 1; i <= RING_SIZE + 256; ++i) mRc[i] = NIL;
    for (u32 i = 0; i <= RING_SIZE; ++i) mPar[i] = NIL;
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

  bool lzmfmx::encode(ByteSource & ubs, ByteSink & cbs) {
    u8 code[17], flags = 0, mask = 1;
    u32 cptr = 1, sid = 0, r = RING_SIZE - MAX_MATCH, n = 0;

    // Prime lookahead buffer
    while (n < MAX_MATCH && !ubs.isEOF())
      mRing[r + n++] = ubs.getNextByteBlocking();

    for (u32 i = 1; i <= MAX_MATCH; ++i)
      insertNode(r - i);
    insertNode(r);

    while (n > 0) {

      u32 ml = mMlen > n ? n : mMlen;
      if (ml <= MIN_MATCH) { ml = 1; flags |= mask; code[cptr++] = mRing[r]; }
      else {
        code[cptr++] = mMpos & 0xFF;
        code[cptr++] = ((mMpos >> 4) & 0xF0) | (ml - MIN_MATCH - 1);
      }
    
      if (!(mask <<= 1)) {
        code[0] = flags;
        for (u32 i = 0; i < cptr; ++i)
          cbs.putNextByteBlocking(code[i]);
        flags = 0;
        mask = 1;
        cptr = 1;
      }

      // Slide window by ml positions
      for (u32 i = 0; i < ml; ++i) {
        deleteNode(sid);
        if (!ubs.isEOF()) {
          u8 c = ubs.getNextByteBlocking();
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
        cbs.putNextByteBlocking(code[i]);
    }
    return true;
  }

#if 0
  bool lzmfmx::encode(ByteSource & ubs, ByteSink & cbs) {
    while (true) {
      s32 s = ubs.getNextByteBlocking();
      if (s >= 0) cbs.putNextByteBlocking((u8) s);
      else break;
    }
    return true;
  }
#endif

  bool lzmfmx::decode(ByteSource & cbs, ByteSink & ubs) {
    memset_s(mRing, '\0', RING_SIZE - MAX_MATCH);
    uint32_t r = RING_SIZE - MAX_MATCH;
    while (!cbs.isEOF()) {
      u32 flags = cbs.getNextByteBlocking() | 0xFF00;
      for (; (flags & 0x100) && !cbs.isEOF(); flags >>= 1) {
        if (flags & 1) {
          u8 c = cbs.getNextByteBlocking();
          //buf_grow(&out, 1);
          ubs.putNextByteBlocking(c);
          mRing[r] = c;
          if (++r >= RING_SIZE) r = 0;
        } else {
          if (cbs.isEOF()) break;
          u32 lo = cbs.getNextByteBlocking();
          if (cbs.isEOF()) break;
          u32 hi = cbs.getNextByteBlocking();
          u32 p = lo | ((hi & 0xF0) << 4);
          u32 ml = (hi & 0x0F) + MIN_MATCH + 1;
          //buf_grow(&out, ml);
          for (u32 k = 0; k < ml; ++k) {
            u32 idx = p + k;
            if (idx >= RING_SIZE) idx -= RING_SIZE;
            u8 c = mRing[idx];
            ubs.putNextByteBlocking(c);
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

