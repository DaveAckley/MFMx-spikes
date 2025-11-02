#pragma once  /* -*- C++ -*- */

#include "itype.h"

namespace MFM {
  enum CarState : u8 {
    UNUSED = 0u,           // under construction
    OPEN,                  // available for (un)loading locally
    CLOSED,                // finished (un)loading locally
    INBOUND_DEPARTED,      // left t6 or arrived host
    OUTBOUND_DEPARTED,     // left host or arrived t6
  };

  struct CarSig {
    static constexpr u16 CARSIG_MAGIC = 0xCAB0;
    u16 mCarMagic;
    u8 mCarNonce;
    u8 mCarState;
    bool isValid() const { return mCarMagic == CARSIG_MAGIC; }
    constexpr CarSig() 
      : mCarMagic(CARSIG_MAGIC)
      , mCarNonce(0u)
      , mCarState(CarState::UNUSED)
    { }
    constexpr CarSig(const CarSig& other) 
      : mCarMagic(other.mCarMagic)
      , mCarNonce(other.mCarNonce)
      , mCarState(other.mCarState)
    { }

    CarSig & operator=(const CarSig & other) {
      mCarMagic = other.mCarMagic;
      mCarNonce = other.mCarNonce;
      mCarState = other.mCarState;
      return *this;
    }
    bool operator==(const CarSig & other) const {
      return isValid()
        && mCarMagic == other.mCarMagic
        && mCarNonce == other.mCarNonce
        && mCarState == other.mCarState
        ;
    }
  };

  template<class CONTENT>
  class alignas(16) BaseCar {
  public:
    bool isComplete() const {
      return
        mHeader.isValid() &&
        mHeader == mFooter;
    }
    bool readyToClose() { return mContent.readyToClose(); }
    CarSig getHeader() const { return mHeader; }
    CarState getCarState() const { return (CarState) mHeader.mCarState; }
    const CONTENT & getContent() const { return mContent; }
    CONTENT & getContent() { return mContent; }
    void setCarState(CarState cs) {
      mHeader.mCarMagic = CarSig::CARSIG_MAGIC;
      mHeader.mCarState = (u8) cs;
      mHeader.mCarNonce++;
      mFooter = mHeader;
    }
  protected:
    CarSig mHeader;
    CONTENT mContent;
    CarSig mFooter;
  };

}
