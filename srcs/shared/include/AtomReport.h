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

    /* EPOCH MARKER.  The report stream is one continuous compressed byte
       stream (a record can straddle cars), so a car cannot carry an epoch.
       Instead, when the hub's epoch (the leader cycle count, scy, low 16
       bits) changes, it puts a marker record in the stream: mCoord.x is
       EPOCH_MARK_X, which no site has, and mCoord.y is the new epoch.  Every
       real record after it, until the next marker, was read in that epoch.
       The host keeps a newest-epoch-per-pixel map and ignores a record
       older than what its pixel already shows, so a late borrower report
       cannot overwrite the owner's newer one (arrival order across streams
       is not under hub control).  Same record size; the spin counter counts
       a marker like any record. */
    static constexpr u16 EPOCH_MARK_X = 0xffffu;

    /* CHANGE STAMP.  A report's epoch (the marker in force) says when it was
       EMITTED, not when the site changed: a backlog of seconds, and a
       borrower's stale cache copy still queued after the SAVE, made an old
       ring report look as new as the owner's later one.  So the high byte of
       mCoord.x (sites have x < 256) carries when the site was last written:
       (changeEpoch & 0x7f) << 1 | phase, phase 0 = SAVE/LOAD, 1 = events.
       The host rebuilds changeEpoch from the stream epoch (the hub clamps
       the age to STAMP_MAX_AGE < 128 epochs, so it is exact) and orders
       reports by (changeEpoch, phase).  Record size is unchanged. */
    static constexpr u16 STAMP_MAX_AGE = 120u;
    static constexpr u8 packStamp(u16 changeEpoch, u16 phase) {
      return (u8) (((changeEpoch & 0x7fu) << 1) | (phase & 1u));
    }
    static constexpr u16 withStamp(u16 x, u8 stamp) {
      return (u16) (x | ((u16) stamp << 8));
    }
    u8 stampByte() const { return (u8) (mCoord.x >> 8); }
    u16 siteX() const { return (u16) (mCoord.x & 0xffu); }
    /** (changeEpoch << 1 | phase) as the host orders it, given the stream's
        newest announced epoch (u16); wraps like the epoch (15 bits). */
    static u16 changeKey(u16 streamEpoch, u8 stamp) {
      const u16 age = (u16) (((streamEpoch & 0x7fu) - (u16) (stamp >> 1)) & 0x7fu);
      const u16 ce = (u16) (streamEpoch - age);
      return (u16) ((ce << 1) | (stamp & 1u));
    }
    bool isEpochMarker() const { return mCoord.x == EPOCH_MARK_X; }
    u16 markerEpoch() const { return (u16) mCoord.y; }
  };

  using AtomReportIO = GObjIO<AtomReport>;

}

