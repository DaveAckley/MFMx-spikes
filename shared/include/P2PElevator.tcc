/* -*- C++ -*- */
#include "Death.h"

namespace MFM {
  template <class CAR, u32 BANKS>
  P2PElevatorPlatform<CAR,BANKS>::P2PElevatorPlatform()
    : mCurrentBank(0u)
  {
    for (u32 i = 0u; i < BANKS; ++i) 
      mBank[i].mPayloadState = PS_UNUSED;
  }

  template <class CAR, u32 BANKS>
  CAR * P2PElevatorPlatform<CAR,BANKS>::getCurrentCarIfAny() {
    if (mCurrentBank >= BANKS) return 0;
    Payload & pay = mBank[mCurrentBank];
    if (pay.mPayloadState != PS_LOADING) return 0;
    return &pay.mCar;
  }

  template <class CAR, u32 BANKS>
  void P2PElevatorPlatform<CAR,BANKS>::initCars(bool hasCars) {
    for (u32 i = 0u; i < BANKS; ++i) 
      mBank[i].mPayloadState = hasCars ? PS_LOADING : PS_DEPARTED;
  }
  
  template <class CAR, u32 BANKS>
  bool P2PElevatorPlatform<CAR,BANKS>::update(ElevatorTransport & et) {
    bool ret = false;
    for (u32 b = 0u; b < BANKS; ++b) {
      Payload& payload = mBank[b];
      switch (payload.mPayloadState) {
      case PS_DEPARTED:
        continue;
      case PS_ARRIVED: 
        FATAL("WRITE ME");
      case PS_LOADING:
        {
          if (payload.mCar.readyToDepart()) {
            u64 toAddress = payload.mReturnAddress;
            payload.mReturnAddress = (u64) (uintptr_t) &payload;
            payload.mPayloadState = PS_ARRIVED;
            et.update(toAddress,payload.mCar);
            payload.mPayloadState = PS_DEPARTED;
            ret = true;
          }
        }
      }
    }
    return ret;
  }
}
