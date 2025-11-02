#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "Fail.h"

namespace MFM {
  struct CarMetaData {
    u32 mArrivalTimeMillis;
    u32 mOccupiedTimeMillis;
  };

  enum CarState : u8 {
    UNUSED = 0u,           // under construction
    OPEN,                  // available for (un)loading locally
    CLOSED,                // finished (un)loading locally
    INBOUND_DEPARTED,      // left t6 or arrived host
    OUTBOUND_DEPARTED,     // left host or arrived t6
  };

  enum CarType : u8 {
    MINCARTYPE = 0u,

    EMPTY = MINCARTYPE,         // header + footer only
    STANDARD,                   // header + CONTENT + footer

    MAXCARTYPE = STANDARD
  };

  struct CarSig {
    static constexpr u8 CARSIG_MAGIC = 0xCA;
    u8 mCarMagic;
    u8 mCarNonce;
    u8 mCarState;
    u8 mCarType;
    CarType getCarType() const { return (CarType) mCarType; }

    bool isValid() const {
      return
        mCarMagic == CARSIG_MAGIC &&
        mCarType >= CarType::MINCARTYPE &&
        mCarType <= CarType::MAXCARTYPE
        ;
    }
    constexpr CarSig(CarType type = CarType::STANDARD ) 
      : mCarMagic(CARSIG_MAGIC)
      , mCarNonce(0u)
      , mCarState(CarState::UNUSED)
      , mCarType(type)
    { }
    constexpr CarSig(const CarSig& other) 
      : mCarMagic(other.mCarMagic)
      , mCarNonce(other.mCarNonce)
      , mCarState(other.mCarState)
      , mCarType(other.mCarType)
    { }

    CarSig & operator=(const CarSig & other) {
      mCarMagic = other.mCarMagic;
      mCarNonce = other.mCarNonce;
      mCarState = other.mCarState;
      mCarType = other.mCarType;
      return *this;
    }
    bool operator==(const CarSig & other) const {
      return isValid()
        && mCarMagic == other.mCarMagic
        && mCarNonce == other.mCarNonce
        && mCarState == other.mCarState
        && mCarType == other.mCarType
        ;
    }
  };

  struct BaseCarMetadata {
    u32 mOccupiedTime;
  };

  template<class CONTENT>
  class alignas(16) BaseCar {
  public:
    bool isComplete() const {
      if (!mHeader.isValid()) return false;
      if (mHeader.mCarType == CarType::STANDARD) return mHeader == mFooter;
      if (mHeader.mCarType == CarType::EMPTY) { // then footer is at &mHeader+1
        const CarSig * pheader = &mHeader;
        return mHeader == pheader[1];
      }
      FAIL(ILLEGAL_STATE);      // we checked isValid dammit
    }
    bool isEmpty() { return mContent.isEmpty(); }
    bool readyToClose() { return mContent.readyToClose(); }
    CarSig getHeader() const { return mHeader; }
    CarSig getStandardFooter() const { return mFooter; }
    CarState getCarState() const { return (CarState) mHeader.mCarState; }
    const CONTENT & getContent() const { return mContent; }
    CONTENT & getContent() { return mContent; }
    void setCarState(CarState cs, CarType ct) {
      mHeader.mCarMagic = CarSig::CARSIG_MAGIC;
      mHeader.mCarState = (u8) cs;
      mHeader.mCarNonce++;
      mHeader.mCarType = ct;
      if (mHeader.mCarType == CarType::STANDARD) mFooter = mHeader;
      else if (mHeader.mCarType == CarType::EMPTY) {
        const CarSig * pheader = &mHeader;
        (const_cast<CarSig*>(pheader))[1] = mHeader;
      } else FAIL(ILLEGAL_STATE);
    }
  protected:
    CarSig mHeader;
    CONTENT mContent;
    CarSig mFooter;
  };

}
