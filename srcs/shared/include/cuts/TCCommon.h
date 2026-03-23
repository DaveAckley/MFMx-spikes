/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"

namespace MFM {

  struct TCCommon {

    enum TCState : u8 {
      UNUSED = 0u,           // 0 under construction
      OPEN,                  // 1 available for (un)loading locally
      CLOSED,                // 2 finished (un)loading locally
      INBOUND_DEPARTED,      // 3 left t6/ewp or arrived host/hub
      OUTBOUND_DEPARTED,     // 4 left host/hub or arrived t6/ewp
    };

    enum TCType : u8 {
      MINTCTYPE = 0u,

      EMPTY = MINTCTYPE,        // header + footer only
      STANDARD,                 // header + CONTENT + footer

      MAXTCTYPE = STANDARD
    };

    struct TCSig {
      static constexpr u8 TCSIG_MAGIC = 0xC5;
      u8 mTCSMagic;
      u8 mTCSNonce;
      u8 mTCSState;
      u8 mTCSType;
      TCType getTCType() const { return (TCType) mTCSType; }
      TCState getTCState() const { return (TCState) mTCSState; }

      void setTCState(TCState state, TCType type) {
        mTCSMagic = TCSIG_MAGIC;
        mTCSState = state;
        mTCSNonce++;
        mTCSType = type;
      }

      bool isValid() const {
        return
          mTCSMagic == TCSIG_MAGIC &&
          mTCSType >= TCType::MINTCTYPE &&
          mTCSType <= TCType::MAXTCTYPE
          ;
      }
      constexpr TCSig(TCType type = TCType::STANDARD ) 
        : mTCSMagic(TCSIG_MAGIC)
        , mTCSNonce(0u)
        , mTCSState(TCState::UNUSED)
        , mTCSType(type)
      { }
      constexpr TCSig(const TCSig& other) 
        : mTCSMagic(other.mTCSMagic)
        , mTCSNonce(other.mTCSNonce)
        , mTCSState(other.mTCSState)
        , mTCSType(other.mTCSType)
      { }

      TCSig & operator=(const TCSig & other) {
        mTCSMagic = other.mTCSMagic;
        mTCSNonce = other.mTCSNonce;
        mTCSState = other.mTCSState;
        mTCSType = other.mTCSType;
        return *this;
      }
      bool operator==(const TCSig & other) const {
        return isValid()
          && mTCSMagic == other.mTCSMagic
          && mTCSNonce == other.mTCSNonce
          && mTCSState == other.mTCSState
          && mTCSType == other.mTCSType
          ;
      }
    };

    struct TCOpsData {
      u32 mArrivalTime;           // in whatever units host vs cross
      u32 mOccupiedTime;          // ditto
    };
  };

}
