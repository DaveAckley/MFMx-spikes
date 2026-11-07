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

    bool isValid() const {
      FAIL(INCOMPLETE_CODE);
      //return mRange.area() < MAX_ATOMS;
    }

    u32 getCurrentPayloadSize() {
      return 0u + (u32) (((u8*) &mAtoms[mAtomCount]) - (u8*) this);
    }

    bool isEmpty() const {
      return mAtomCount == 0;
    }

    bool isFull() const {
      return mAtomCount == MAX_ATOM_INDEX; 
    }

    void setToZeroAtoms() {
      mAtomCount = 0;
    }

    void pushAtomAtEndOfPacketOrDie(const P4Atom a) {
      MFM_API_ASSERT(!isFull(), OUT_OF_ROOM);
      mAtoms[mAtomCount++] = a;
    }

    u32 getAtomsInPacket() const {
      return mAtomCount;
    }

    P4Atom getAtomFromPacketOrDie(u8 idx) {
      //HBASSERT_LE(idx,MAX_ATOM_INDEX); // tautology
      return mAtoms[idx];
    }

    u8 mSrcCode;             //< source sites (dir8: 0..7) or caches (dir8+8: 8..15) of these atoms
    u8 mAtomCount;           //< #atoms in this mAtoms
    U8C mStartsAt;           //< mAtoms[0] is from this U8C

    static constexpr u32 MAX_ATOM_INDEX = U8_MAX;
    P4Atom mAtoms[MAX_ATOM_INDEX+1];
  };

  struct InterHubPayload {

    void init() {
      memset_s(this,'\0',sizeof(*this));
    }
    
    void initType(IHPType type) {
      init();                   // clear all
      initIHP(type,{0,0},{0,0}); // ct6 dest and src UNUSED
    }

    IHPType getType() const {
      MFM_API_ASSERT(isValid(),ILLEGAL_STATE);
      return mIHPHeader.mPayloadType;
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

    IHPAtoms & asAtomsOrDie() {
      MFM_API_ASSERT(isValid(), ILLEGAL_STATE);
      HBASSERT_EQ(mIHPHeader.mPayloadType,IHPT_ATOMS);
      return *(IHPAtoms*) &mIHPData[0];
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
      initType(IHPT_EMPTY);
    }
    IHPType getType() {
      return payload().getType();
    }
    void initType(IHPType type) {
      TC::reset(); // sets state 0==UNUSED
      openTC();    // set state open
      payload().initType(type);       // whatever type
      closeTC(sizeof(payload())); // and then close it, with a full load
      setDepartingTC(TCState::OUTBOUND_DEPARTED); // init state is 'departed in'/'arrived out'
    }
  };
  static_assert(sizeof(InterHubBlock)==4160,"InterHubBlock size check failed");

  typedef TCStorage<InterHubBlock,4> InterHubStorage;
  static_assert(sizeof(InterHubStorage)==16640,"InterHubStorage size check failed");
}
