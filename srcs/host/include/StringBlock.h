#pragma once      /* -*- C++ -*- */
#include "itype.h"
#include "P4Atom.h"
#include "S32C.h"

namespace MFM {
  struct StringBlock {
    StringBlock()
      : mBytes(0)
      , mLen(0)
      , mRowLen(0)
      , mFillPos(0)
      , mDims({0,0})
    {}

    ~StringBlock() { clear(); }

    void displayAtom(S32C dtcoord, S32C dsize, S32C sgcoord, S32C ssize, const P4Atom & a) {
      u16 t = a.getType();
      char xch = ' ', ych = ' ';
      u32 div;
      if (ssize.x < 3u) div = 5u;
      else if (ssize.x < 13u) div = 50u;
      else div = 500u;

      // NO AXES IN HERE
      //      if ((sgcoord.x-ssize.x/2u)/div != (sgcoord.x+ssize.x/2u)/div) xch = '.';
      //      if ((sgcoord.y-ssize.y/2u)/div != (sgcoord.y+ssize.y/2u)/div) ych = '.';

      char ch;
      if (xch != ' ' && ych != ' ') ch = '+';
      else if (xch != ' ') ch = xch;
      else ch = ych;

      char dch;
      switch (t) {
      case 0: dch = ' '; break;
      case P4Atom::INACCESSIBLE_TYPE:
        //dch = '|'; ch = '_';
        dch = '/'; ch = '\\';
        //dch = '.'; ch = ':';
        if (dtcoord.y & 1) { char tch = dch; dch = ch; ch = tch; }
        break;
      default: dch = '0'+t; break;
      }
      put2D(dtcoord,dch);
      put2D(dtcoord+S32C({1,0}), ch);
    }

    void clear() {
      delete [] mBytes;
      mBytes = 0;
      mLen = 0;
      mDims = { 0,0 };
      mFillPos = 0u;
    }

    void append(std::string s) {
      const char * data = s.data();
      u32 len = s.size();
      while (len-->0) 
        if (mFillPos < mLen) mBytes[mFillPos++] = *data++;
    }

    bool put2D(S32C at, std::string s, bool erase = false) {
      const char * data = s.data();
      u32 len = s.size();
      while (len-->0) {
        if (put2D(at,*data++,erase)) at.x++;
        else return false;
      }
      return true;
    }

    bool put2D(S32C at, char c, bool erase = false) {
      if (at.x < 0 || at.x >= mDims.x ||
          at.y < 0 || at.y >= mDims.y)
        return false;
      char * p = &mBytes[at.y * mRowLen + at.x];
      if (c != ' ' || erase)
        if (*p != '\n') *p = c;
      return true;
    }

    void init(S32C dims) {
      if (dims != mDims) {
        clear();
        mDims = dims;
        mRowLen = mDims.x+1u;
        mLen = mRowLen*mDims.y;
        mBytes = new char [mLen+1]; // +1 for null
      }
      mFillPos = 0u;
      memset_s(mBytes,' ',mLen);
      for (u32 r = 0u; r < mDims.y; ++r) {
        mBytes[r*mRowLen+0] = '>'; // DEBUG
        mBytes[r*mRowLen+mDims.x-1] = '|'; // DEBUG
        mBytes[r*mRowLen+mDims.x] = '\n';
      }
      mBytes[mLen] = '\0';
    }

    std::string_view asSV() { return std::string_view(mBytes,mLen); }

    char * mBytes;
    u32 mLen;
    u32 mRowLen;
    u32 mFillPos;
    S32C mDims;
  };
}
