#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"
#include "EventWindow.h"
#include "Debug.h"
#include "TCStorage.h"
#include "S16C.h"

namespace MFM {

  enum IHPType : u8 {
    IHPT_EMPTY = 0x00,
    IHPT_PING =  0x01,
    IHPT_ATOMS = 0x02,
    IHPT_DATA =  0x03,
    MAX_IHPT
  };

  constexpr const char * getNameFromIHPType(IHPType ih) {
    switch (ih) {
    case IHPT_EMPTY: return "EMPTY(0)";
    case IHPT_PING: return "PING(1)";
    case IHPT_ATOMS: return "ATOMS(2)";
    case IHPT_DATA: return "DATA(3)";
    default: return "IHPT_WHAAAT(?)";
    }
  }

  struct IHPBase {
  };
  
  struct IHPPing : public IHPBase {
    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        return false;
      }
      // WRITE ME XXXX
      return true;
    }

    bool isValid() const {
      // WRITE ME XXX
      return true;
    }

    u32 getCurrentPayloadSize() {
      return 0u + (u32) (((u8*) &mHopReports[mHopReportsUsed]) - (u8*) this);
    }

    struct HopReport {
      S8C mFromC;
      S8C mAtC;
      S8C mNextC;
      enum HopStatus : u8 {
        HOPST_UNINIT = 0,
        HOPST_OUTBOUND,
        HOPST_AT_GOAL,
        HOPST_INBOUND,
        HOPST_NO_FWD,
        HOPST_COUNT
      };
      HopStatus mResult;
    };
    static constexpr u32 MAX_HOPRS = U8_MAX;
    u8 mHopReportsUsed;
    HopReport mHopReports[MAX_HOPRS];
  };

  struct IHPAtoms : public IHPBase {
    void init() {
      memset_s(this,'\0',sizeof(*this));
    }

    bool update(bool inside) { 
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        return false;
      }
      if (inside) {
        ++mOrigin.x;
      } else {
        ++mOrigin.y;
      }
      return true;
    }

    bool isValid() const {
      return mRange.area() < MAX_ATOMS;
    }

    u32 getCurrentPayloadSize() {
      return 0u + (u32) (((u8*) &mAtoms[mDataUsed]) - (u8*) this);
    }

    S16C mOrigin;               //< origin relative coord, updates on transits
    U16CRange mRange;
    u16 mDataUsed;
    static constexpr u32 MAX_ATOMS = (((1u<<12) - sizeof(mOrigin) - sizeof(mRange)) / sizeof(P4Atom)) - 1;
    P4Atom mAtoms[MAX_ATOMS];
  };

  struct InterHubPayload {

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }
    
    u32 getCurrentPayloadSize() {
      MFM_API_ASSERT(isValid(),ILLEGAL_STATE);
      switch (mIHPHeader.mPayloadType) {
      case IHPT_EMPTY:
        return sizeof(mIHPHeader);

      case IHPT_PING:
        return sizeof(mIHPHeader) + ((IHPPing*) &mIHPData[0])->getCurrentPayloadSize();

      case IHPT_ATOMS:
        return sizeof(mIHPHeader) + ((IHPAtoms*) &mIHPData[0])->getCurrentPayloadSize();

      default:
        FAIL(ILLEGAL_STATE);
      }
    }

    bool tryRoute(Dir4 d4, bool inside) {
      LOGPTAG(IHPtR,mIHPHeader.mHops2Dest);
      char str[2];
      LOGPTAG(IHPd4,dir4ToByteCodeStr(str,d4));
      // XXX WRITE ME
      return false;
    }

    bool update(bool inside) { 
      MFM_API_ASSERT(isValid(),ILLEGAL_STATE);
      EACH(1'000'000,LOGPTAG(upIHC10,this));
      if (!isValid()) {
        HBPTAG(ihpxx,this);
        return false;
      }
      EACH(1'000'000,LOGPTAG(upIHC11,getNameFromIHPType(mIHPHeader.mPayloadType)));
      ///// FIRST DO ROUTING IF NEEDED, FOR ALL PAYLOAD TYPES

      IHPHeader & h = mIHPHeader;
      if (h.mHops2Dest.length() > 0) {
        // which way do we go george?

        // maybe for starters don't be random? so
        // we can have hope that packet sequences
        // will arrive in order?

        U8C steps = h.mHops2Dest.abs();
        if (steps.x >= steps.y) { // go EW
          if (h.mHops2Dest.x > 0) return tryRoute(D4_E,inside);
          if (h.mHops2Dest.x < 0) return tryRoute(D4_W,inside);
          FAIL(UNREACHABLE_CODE); // hey, dest.length() > 0...
        } else /* steps.y > steps.x */ {     // go NS
          if (h.mHops2Dest.y > 0) return tryRoute(D4_S,inside);
          if (h.mHops2Dest.y < 0) return tryRoute(D4_N,inside);
          FAIL(UNREACHABLE_CODE); // hey, dest.length() > 0...
        }
      }

      EACH(1'000'000,LOGPTAG(upIHC@dest,h.mPayloadType));

      ///// WE'RE STILL HERE AFTER ROUTING
      switch (h.mPayloadType) {
      case IHPT_EMPTY: return false;

      case IHPT_PING:
        EACH(1,LOGPTAG(upIHC12,&mIHPData[0]));
        return ((IHPPing*) &mIHPData[0])->update(inside);

      case IHPT_ATOMS:
        EACH(1,LOGPTAG(upIHC12,&mIHPData[0]));
        return ((IHPAtoms*) &mIHPData[0])->update(inside);

      default:
        FAIL(ILLEGAL_STATE);
      }
    }

    bool isValid() const {
      return mIHPHeader.isValid();
    }

    u8 initIHP(IHPType type,S8C ct6dest, S8C ct6src) {
      return mIHPHeader.initIHP(type,ct6dest,ct6src);
    }

    struct IHPHeader {
      u8 mCmdSpinner1;
      IHPType mPayloadType;
      u8 mHopsDistance;
      u8 mCmdSpinner2;

      S8C mHops2Dest, mHops2Source;

      u8 initIHP(IHPType type,S8C hops2dest, S8C hops2source) {
        mPayloadType = type;
        mHops2Dest = hops2dest;
        mHops2Source = hops2source;
        mHopsDistance = mHops2Dest.length() + mHops2Source.length();
        mCmdSpinner2 = ++mCmdSpinner1;
        return mCmdSpinner1;
      }

      bool isValid() const {
        return
          mPayloadType < MAX_IHPT &&
          mCmdSpinner1 == mCmdSpinner2 &&
          mHops2Dest.length() + mHops2Source.length() == mHopsDistance;
      }
    };
    
    static constexpr u32 MAX_BYTES = (1u<<12) - sizeof(IHPHeader);

    IHPHeader mIHPHeader;
    u8 mIHPData[MAX_BYTES];
  };
  
  class InterHubBlock : public TC<InterHubBlock,sizeof(InterHubPayload)> {
  public:
    const char * getName() const { return "InterHubBlock"; }

    bool readyToClose(TCOpsData & tms,u32 msnow) const { 
      FAIL(INCOMPLETE_CODE);
    }
    InterHubPayload & payload() { return *(InterHubPayload*) getDataStart(); }
    void init() {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().init(); // sets IHPT_EMPTY
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };
  static_assert(sizeof(InterHubBlock)==4160,"InterHubBlock size check failed");

  typedef TCStorage<InterHubBlock,4> InterHubStorage;
  static_assert(sizeof(InterHubStorage)==16640,"InterHubStorage size check failed");
}
