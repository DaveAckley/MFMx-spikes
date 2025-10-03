/* -*- C++ -*- */
#pragma once

#include "itype.h"

namespace MFM {
  struct CarHeader {
    enum CarStatus : u8 {
      CH_UNUSED,
      CH_ARRIVED,
      CH_OPEN,
      CH_DEPARTED
    };

    u8 mStatus : 6;
    u32 mAddressHi : 26;
    u32 mAddressLo : 32;
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

#if 0
  template <u32 MINBYTES>
  struct Car {
    static constexpr u32 BYTE_SIZE = ((MINBYTES+15u)/16u)*16u;
    static_assert(BYTE_SIZE > 0u);
    u64 mReturnAddress;

    static constexpr u32 DATA_SIZE = BYTE_SIZE - sizeof(mReturnAddress);
    u8 mData[DATA_SIZE];
  };
#endif

  struct LogBlock {
    u8 mLength;
    u8 mData[250];
  };
  struct EWBlock {
    typedef u32 EWAtom[4];
    EWAtom mOld[41];
    EWAtom mNew[41];
  };

  struct TransportBlock {
    typedef TCar<LogBlock> LogCar;
    LogCar mLogCars[3];

    typedef TCar<EWBlock> EWCar;
    EWCar mEWCars[2];
  };
}
