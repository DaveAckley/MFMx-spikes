/* -*- C++ -*- */
#pragma once

#include <cstring>
#include "itype.h"
#include "BaseCar.h"
#include "Fail.h"

namespace MFM {

  struct LogBlock {
    static constexpr u8 MAX_LEN = U8_MAX-9u;
    u8 mLength;
    u8 mData[MAX_LEN];

    bool readyToClose() const {
      u32 left = spaceRemaining();
      return
        (left < MAX_LEN/10) ||
        ((left < MAX_LEN/4) && lastByte() == '\n');
    }

    u32 spaceRemaining() const { return MAX_LEN - mLength; }
    void reset() { mLength = 0u; }
    s32 lastByte() const {
      if (isEmpty()) return -1;
      return mData[mLength];
    }
    bool isEmpty() const { return mLength == 0u; }
    bool isFull() const { return mLength >= MAX_LEN; }
    bool addByte(u8 b) {
      if (isFull()) return false;
      mData[mLength++] = b;
      return true;
    }
    bool removeByte() {
      if (isEmpty()) return false;
      mData[mLength--] = 0u;
      return true;
    }
  };

  struct EWBlock {
    bool readyToClose() const { // Closing (to be) handled by ew processing
      return false;
    }
    typedef u32 EWAtom[4];
    EWAtom mOld[41];
    EWAtom mNew[41];
  };

  struct LogCarStorage {
    LogCarStorage() {
      memset(&mLogCars,0u,sizeof(mLogCars));
    }
    typedef BaseCar<LogBlock> LogCar;
    static constexpr u32 CAR_COUNT = 2u;
    LogCar mLogCars[CAR_COUNT];
  };
  extern LogCarStorage theLogCarStorage;
  
  struct EWCarStorage {
    typedef BaseCar<EWBlock> EWCar;
    static constexpr u32 CAR_COUNT = 3u;
    EWCar mEWCars[CAR_COUNT];
  };    
  extern EWCarStorage theEWCarStorage;

  struct TransportBlock {
    u32 mLogCarStorageT6Ptr;
    u32 mEWCarStorageT6Ptr;
  };
}
