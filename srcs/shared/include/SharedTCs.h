#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "TC.h"

namespace MFM {
  struct LogBlock : public TC<LogBlock> {
    static constexpr u8 MAX_LEN = U8_MAX-9u;
    u8 mLength;
    u8 mData[MAX_LEN];

    bool readyToClose(CarOpsTimers & tms,u32 msnow) const {
      u32 left = spaceRemaining();
      if (mLength > 0u && (msnow - tms.mOccupiedTime) >= 2000)
        return true; // don't sit occupied more than 2 seconds
      bool ret =
        (left < MAX_LEN/10) ||
        ((left < MAX_LEN/4) && lastByte() == '\n');
      return ret;
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

  class EWBlock : public TC<EWBlock> {
  public:
    bool readyToClose(CarOpsTimers & tms,u32 msnow) const { // Closing (to be) handled by ew processing
      return false;
    }
    bool isEmpty() const {
      return false;             // no empty ews (maybe 'half full ews', one day)
    }
    void reset() {
      memset_s(this,0u,sizeof(this)); // toyota
    }
    EventWindow mOld, mNew;
  private:
    friend class EWControl; // host side only
    friend class OurTLBs;   // host side only
    s32 mHiddenXPos, mHiddenYPos; // host side use only
    TimeStamp mSTVLTime;          // host side use only
  };

  struct LogCarStorage {
    LogCarStorage() {
      memset_s(&mLogCars,0u,sizeof(mLogCars));
    }
    typedef BaseCar<LogBlock> LogCar;
    static constexpr u32 CAR_COUNT = 16u;
    LogCar mLogCars[CAR_COUNT];
    LogCar & getLogCar(u32 carnum) {
      MFM_API_ASSERT(carnum < CAR_COUNT,ILLEGAL_ARGUMENT);
      return mLogCars[carnum];
    }
  };
  extern LogCarStorage theLogCarStorage[1];
  
  struct LogCarMetadata {
    LogCarMetadata() {
      memset_s(&mLogData,0u,sizeof(mLogData));
    }
    static constexpr u32 CAR_COUNT = LogCarStorage::CAR_COUNT;
    CarOpsTimers mLogData[CAR_COUNT];
  };

  struct EWCarStorage {
    typedef BaseCar<EWBlock> EWCar;
    static constexpr u32 CAR_COUNT = 2u;
    EWCar mEWCars[CAR_COUNT];
  };    

  struct EWCarMetadata {
    EWCarMetadata() {
      memset_s(&mEWData,0u,sizeof(mEWData));
    }
    static constexpr u32 CAR_COUNT = EWCarStorage::CAR_COUNT;
    CarOpsTimers mEWData[CAR_COUNT];
  };

  struct TransportBlockOBSOLETE {
    u32 mLogCarStorageT6Ptr;
    u32 mEWCarStorageT6Ptr;
  };
}
