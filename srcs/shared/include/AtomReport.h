#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "P4Atom.h"
#include "UxC.h"

namespace MFM {

  template <class OBJ>
  struct GObjIO {
    using Obj = OBJ;   // the class being I&|O'd
    GObjIO() { init(); }
    static constexpr u32 OBJ_SIZE = sizeof(Obj);

    void init() {
      mPos = -1;
    }

    bool canGetObj() { return mPos == OBJ_SIZE; }
    bool tryGetObj(Obj & o) {
      if (!canGetObj()) return false;
      o = mObj;
      mPos = -1;
      return true;
    }
    void getObj(Obj & o) {
      MFM_API_ASSERT(canGetObj(),ILLEGAL_STATE);
      getObj(o);
    }

    bool canPutObj() { return mPos == -1 || mPos == OBJ_SIZE; }
    bool tryPutObj(const Obj & o) {
      if (!canPutObj()) return false;
      mObj = o;
      mPos = 0;
      return true;
    }
    void putObj(const Obj & o) {
      MFM_API_ASSERT(canPutObj(),ILLEGAL_STATE);
      tryPutObj(o);
    }

    bool nextIsFirstObjectByte() { return mPos == 0; }
    bool nextIsLastObjectByte() { return mPos == OBJ_SIZE-1; }

    bool canGetByte() { return mPos >= 0 && (u32) mPos < OBJ_SIZE; }
    bool tryGetByte(u8 & b) {
      if (!canGetByte()) return false;
      b = ((u8*) &mObj)[mPos++];
      return true;
    }
    
    bool canPutByte() { return mPos >= 0 && mPos < OBJ_SIZE; }
    bool tryPutByte(const u8 & b) {
      if (!canPutByte()) return false;
      ((u8*) &mObj)[mPos++] = b;
      return true;
    }
    
  private:
    Obj mObj;
    s32 mPos;
  };

  struct AtomReport {
    P4Atom mAtom;
    U16C mCoord;
  };

  using AtomReportIO = GObjIO<AtomReport>;

#if 0
  template <class SUBC,class OBJ>
  struct ObjIO {
    using Self = SUBC; // the class performing I&|O
    using Obj = OBJ;   // the class being I&|O'd
    
    // self(): access this by subtype
    Self& self() { return static_cast<Self&>(*this); }
    Self const & self() const { return static_cast<Self const&>(*this); }

    // "API": Self must implement all of these!
    const char * getName() const { return self().getName(); }

    bool canGetObj() { return self().canGetObj(); }
    bool tryGetObj(Obj & o) { return self().tryGetObj(o); }

    bool canPutObj() { return self().canPutObj(); }
    bool tryPutObj(const Obj & o) { return self().tryPutObj(o); }

    bool canGetByte() { return self().canGetByte(); }
    bool tryGetByte(u8 & b) { return self().tryGetByte(b); }
    
    bool canPutByte() { return self().canPutByte(); }
    bool tryPutByte(const u8 & b) { return self().tryPutByte(b); }

  };


  struct AtomReportIO : public ObjIO<AtomReportIO,AtomReport> {
    AtomReportIO() : mPos(-1) { }
    const char * getName() const { return "ARIO"; }
    bool canGetObj() { return mPos == sizeof(Obj); }
    bool tryGetObj(Obj & o) {
      if (!canGetObj()) return false;
      o = mObj;
      mPos = -1;
      return true;
    }

    bool canPutObj() { return mPos < 0; }
    bool putObj(const Obj & o) {
      if (!canPutObj()) return false;
      mObj = o;
      mPos = 0;
      return true;
    }

  private:
    Obj mObj;
    s32 mPos;
  };
#endif

#if 0
  struct AtomReportSource : public ObjIO<AtomReportSource,AtomReport> {
    AtomReportSource() : mPos(-1) { }
    const char * getName() const { return "ASrc"; }
    bool canGetObj() { return mPos == sizeof(Obj); }
    bool tryGetObj(Obj & o) {
      if (!canGetObj()) return false;
      o = mObj;
      mPos = -1;
      return true;
    }

    bool canPutObj() { return false; }
    bool putObj(const Obj & o) { return false; }

  private:
    Obj mObj;
    s32 mPos;
  };

  struct AtomReportSink : public ObjIO<AtomReportSink,AtomReport> {
    AtomReportSink() : mPos(-1) { }
    const char * getName() const { return "ASnk"; }
    bool canGetObj() { return false; }
    bool tryGetObj(Obj & o) { return false; }

    bool canPutObj() { return mPos < 0; }
    bool tryPutObj(const Obj & o) {
      if (!canPutObj()) return false;
      mObj = o;
      mPos = 0;
      return true;
    }

  private:
    static constexpr u32 OBJLEN = sizeof(Obj);
    Obj mObj;
    s32 mPos;
  };
#endif

}

