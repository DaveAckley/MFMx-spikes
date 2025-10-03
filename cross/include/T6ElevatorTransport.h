#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "U16C.h"
#include "ExtraConstants.h"
#include "HostBlock.h"
#include "TransportBlock.h"

namespace MFM {

  template <typename TYPE>
  struct TCarManager {
    void init(TYPE & base, u32 count) ;

    u32 mTCarCount;
    u32 mCurrentIndex;
    TYPE * mTCarBasePtr;
    void update() ;
  };

  struct T6ElevatorTransport {
    // We have P150B boards, so we are using the PCIe 0 tile, which is
    // at (2,0) in raw NoC0 coords
    static constexpr U16C PCIeTILE_COORD = {2,0};

    void init(HostBlock & hb, TransportBlock & tb) {
      mHostBlockPtr = &hb;
      mTransportBlockPtr = &tb;
      mLogCarManager.init(tb.mLogCars[0],tb.MAXLOGCARS);
    }
    void updateTransportBlock() ;
    bool allClear() ;
    s32 initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) ;
    s32 initiateWriteToHost(u32 * data, u32 count, u64 destaddr) {
      //// NOTE WE ARE NOT DEALING WITH THE PCIe TRANSACTION
      //// ATTRIBUTES STUFF IN THE TOP SIX BITS OF destaddr! WHATEVER
      //// THE CALLER PROVIDES WE JUST GO WITH.
      return initiateWrite(data, count, PCIeTILE_COORD, destaddr);
    }

    HostBlock * mHostBlockPtr;
    TransportBlock * mTransportBlockPtr;

    typedef TCarManager<TransportBlock::LogCar> LogCarManager;
    LogCarManager mLogCarManager;
  };
}

#include "T6ElevatorTransport.tcc"


