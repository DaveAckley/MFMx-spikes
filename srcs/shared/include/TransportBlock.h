/* -*- C++ -*- */
#pragma once

#include "itype.h"

#include <cstring>
#include "BaseCar.h"
#include "Fail.h"
#include "EventWindow.h"
#include "TimeDefs.h"

//#include "CrossUtils.h"

namespace MFM {

  template<class CONTENT>
  struct TransportableContent {
    bool readyToClose(BaseCarMetadata & meta,u32 msnow) const {
      return static_cast<const CONTENT*>(this)->readyToClose(meta,msnow);
    }
    bool isEmpty() { return static_cast<const CONTENT*>(this)->isEmpty(); }
    void reset() { static_cast<const CONTENT*>(this)->reset(); }
  protected:
    TransportableContent() = default; // don't make these
  };

  struct LogBlock : public TransportableContent<LogBlock> {
    static constexpr u8 MAX_LEN = U8_MAX-9u;
    u8 mLength;
    u8 mData[MAX_LEN];

    bool readyToClose(BaseCarMetadata & meta,u32 msnow) const {
      u32 left = spaceRemaining();
      if (mLength > 0u && (msnow - meta.mOccupiedTime) >= 2000)
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

  class EWBlock : public TransportableContent<EWBlock> {
  public:
    bool readyToClose(BaseCarMetadata & meta,u32 msnow) const { // Closing (to be) handled by ew processing
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
  };
  extern LogCarStorage theLogCarStorage[1];
  
  struct LogCarMetadata {
    LogCarMetadata() {
      memset_s(&mLogData,0u,sizeof(mLogData));
    }
    static constexpr u32 CAR_COUNT = LogCarStorage::CAR_COUNT;
    BaseCarMetadata mLogData[CAR_COUNT];
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
    BaseCarMetadata mEWData[CAR_COUNT];
  };

  struct TransportBlock {
    u32 mLogCarStorageT6Ptr;
    u32 mEWCarStorageT6Ptr;
  };
}
