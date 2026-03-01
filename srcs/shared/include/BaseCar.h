#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "Fail.h"

namespace MFM {

  enum CarState : u8 {
    UNUSED = 0u,           // 0 under construction
    OPEN,                  // 1 available for (un)loading locally
    CLOSED,                // 2 finished (un)loading locally
    INBOUND_DEPARTED,      // 3 left t6/ewp or arrived host/hub
    OUTBOUND_DEPARTED,     // 4 left host/hub or arrived t6/ewp
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
    u32 mArrivalTime;           // in whatever units host vs cross
    u32 mOccupiedTime;          // ditto
  };

  template<class CONTENT>
  class alignas(16) BaseCar {
  public:
    CarSig getFooter() const {
      u8 t = mHeader.mCarType;
      if (t == CarType::STANDARD) return mFooter;
      if (t == CarType::EMPTY) { // then footer is at &mHeader+1
        const CarSig * pheader = &mHeader;
        return pheader[1];
      }
      return CarSig();          // type standard state unused
    }
    bool isComplete() const {
      CarSig hdrsnap = mHeader;
      if (!hdrsnap.isValid()) return false;
      CarSig footer = getFooter();
      return mHeader == footer;

      HOST_FATAL(ILLEGAL_STATE, "BAD 'VALID' HEADER: magic=%x, nonce=%d, state=%d, type=%d\n",
                 hdrsnap.mCarMagic,
                 hdrsnap.mCarNonce,
                 hdrsnap.mCarState,
                 hdrsnap.mCarType);
      return false; // NOT REACHED
    }
    bool isEmpty() { return mContent.isEmpty(); }
    bool readyToClose(BaseCarMetadata & meta, u32 msnow) { return mContent.readyToClose(meta,msnow); }
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
