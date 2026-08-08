#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "P4Atom.h"
#include "UxC.h"

namespace MFM {

  template <class OBJ>
  struct GObjIO {
    using Obj = OBJ;   // the class being I&|O'd
    GObjIO() { init(true); }
    static constexpr u32 OBJ_SIZE = sizeof(Obj);
    static constexpr u32 OBJ_NONE = U32_MAX; // Flag: Have no object

    void init(bool serialize) {
      if (serialize) discardObj();
      else discardBytes();
    }

    bool discardObj() {         // to make room for another object
      bool ret = canGetObj();
      mPos = OBJ_NONE;
      return ret;
    }

    bool canGetObj() { return mPos == OBJ_SIZE; }
    bool tryGetObj(Obj & o) {
      if (!canGetObj()) return false;
      o = mObj;
      mPos = OBJ_NONE;
      return true;
    }
    void getObj(Obj & o) {
      if (!tryGetObj(o))
        FAIL(ILLEGAL_STATE);
    }

    bool canPutObj() { return mPos == OBJ_NONE; }
    bool tryPutObj(const Obj & o) {
      if (!canPutObj()) return false;
      mObj = o;
      mPos = 0;
      return true;
    }
    void putObj(const Obj & o) {
      if (!tryPutObj(o))
        FAIL(ILLEGAL_STATE);
    }

    bool nextIsFirstObjectByte() { return mPos == 0; }
    bool nextIsLastObjectByte() { return mPos == OBJ_SIZE-1; }

    bool canGetByte() { return mPos < OBJ_SIZE; }
    bool tryGetByte(u8 & b) {
      if (!canGetByte()) return false;
      b = ((u8*) &mObj)[mPos++];
      return true;
    }
    
    bool canPutByte() { return mPos < OBJ_SIZE; }
    bool tryPutByte(const u8 & b) {
      if (!canPutByte()) return false;
      if (mPos == OBJ_NONE) mPos = 0;
      ((u8*) &mObj)[mPos++] = b;
      return true;
    }

    bool discardBytes() {       // to make room for more bytes
      bool ret = canGetByte();
      mPos = 0;
      return ret;
    }
    
  private:
    //    u32 _getPos() const { return mPos; } // can we ditch getPos?

    Obj mObj;
    u32 mPos;
  };

  struct AtomReport {
    P4Atom mAtom;
    U16C mCoord;
    u16 mSpin1,mSpin2;

    bool isValid(u16 expected) {
      // NOTE: NOT CHECKING mAtom.isValid()!
      // Here at the transport level,
      // everything but the spins are 8BC.
      return mSpin1 == mSpin2 && mSpin1 == expected;
    }
  };

  using AtomReportIO = GObjIO<AtomReport>;

}

