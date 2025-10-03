/* -*- C++ -*- */
#pragma once

#include "itype.h"

namespace MFM {
  struct CarHeader {
    enum CarStatus : u8 {
      CH_UNUSED = 0u,           // slot invalid, bss state,
      CH_ARRIVED,               // slot occupied, car arrived from far end
      CH_OPEN,                  // slot occupied, car open for content exchange
      CH_CLOSED,                // slot occupied, car done with content exchange
      CH_DEPARTED,              // slot empty, car shipped to far end
    };

    u8 mStatus : 6;             // CarStatus
    u32 mAddressHi : 26;        // 'return address' - far end hi bits +
    u32 mAddressLo : 32;        // far end lo bits, if slot is occupied
  };

  template <typename TYPE>
  struct TCar {
    CarHeader mHeader;

    static constexpr u32 OBJ_BYTES = sizeof(TYPE);
    static constexpr u32 MIN_BYTES = OBJ_BYTES + sizeof(mHeader);
    static constexpr u32 BYTE_SIZE = ((MIN_BYTES+15u)/16u)*16u - sizeof(mHeader);

    union {
      TYPE mObject;
      u8 mData[BYTE_SIZE];
    } u;
  };

  struct LogBlock {
    static constexpr u8 MAX_LEN = U8_MAX-10u;
    u8 mLength;
    u8 mData[MAX_LEN];

    void reset() { mLength = 0u; }
    bool isEmpty() const { return mLength == 0u; }
    bool isFull() const { return mLength >= MAX_LEN; }
    bool addByte(u8 b) {
      if (isFull()) return false;
      mData[mLength++] = b;
      return true;
    }
  };

  struct EWBlock {
    typedef u32 EWAtom[4];
    EWAtom mOld[41];
    EWAtom mNew[41];
  };

  struct TransportBlock {
    typedef TCar<LogBlock> LogCar;
    static constexpr u32 MAXLOGCARS = 3u;
    LogCar mLogCars[MAXLOGCARS];

    typedef TCar<EWBlock> EWCar;
    static constexpr u32 MAXEWCARS = 2u;
    EWCar mEWCars[MAXEWCARS];

  };
}
