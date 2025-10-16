#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "U16C.h"
#include "ExtraConstants.h"
#include "HostBlock.h"
#include "TransportBlock.h"
#include "AtomicLock.h"
#include "P2PElevator.h"

namespace MFM {

  struct T6ElevatorTransport {
    // We have P150B boards, so we are using the PCIe 0 tile, which is
    // at (2,0) in raw NoC0 coords
    static constexpr U16C PCIeTILE_COORD = {2,0};

    void init(HostBlock & hb, TransportBlock & tb) {
      mHostBlockPtr = &hb;
      mTransportBlockPtr = &tb;
      u64 noc = hb.getHostNocAddr();
      mP2PLogCarManager.initCars((LogCarStorage::LogCar*) tb.mLogCarStorageT6Ptr,
                                 LogCarStorage::CAR_COUNT,
                                 noc + (tb.mLogCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                                 false);
      mP2PEWCarManager.initCars((EWCarStorage::EWCar*) tb.mEWCarStorageT6Ptr,
                                EWCarStorage::CAR_COUNT,
                                noc + (tb.mEWCarStorageT6Ptr - tb.mLogCarStorageT6Ptr),
                                false);
    }
    bool updateTransportBlock() ;
    bool allClear() ;
    bool ship(LogCarStorage::LogCar & lc, u32 carnum) ;
    bool ship(EWCarStorage::EWCar & lc, u32 carnum) ;
    s32 initiateWrite(u32 * data, u32 count, U16C xy, u64 destaddr) ;
    s32 initiateWriteToHost(u32 * data, u32 count, u64 destaddr) {
      //// NOTE WE ARE NOT DEALING WITH THE PCIe TRANSACTION
      //// ATTRIBUTES STUFF IN THE TOP SIX BITS OF destaddr! WHATEVER
      //// THE CALLER PROVIDES WE JUST GO WITH.
      return initiateWrite(data, count, PCIeTILE_COORD, destaddr);
    }

    HostBlock * mHostBlockPtr;
    TransportBlock * mTransportBlockPtr;

    typedef P2PElevatorPlatform<LogCarStorage::LogCar,T6ElevatorTransport> P2PLogCarManager;
    P2PLogCarManager mP2PLogCarManager;

    typedef P2PElevatorPlatform<EWCarStorage::EWCar,T6ElevatorTransport> P2PEWCarManager;
    P2PEWCarManager mP2PEWCarManager;

  };

  extern T6ElevatorTransport theT6ElevatorTransport;
}


