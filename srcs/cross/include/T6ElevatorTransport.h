#pragma once                  // -*- C++ -*-
#include "itype.h"
#include "U16C.h"
#include "CrossUtils.h"
#include "ExtraConstants.h"
#include "HostBlock.h"
#include "TransportBlock.h"
#include "AtomicLock.h"
#include "P2PElevator.h"

namespace MFM {

  struct T6ElevatorTransport : public ElevatorTransportBase {
    // We have P150B boards, so we are using the PCIe 0 tile, which is
    // at (2,0) in raw NoC0 coords
    static constexpr U16C PCIeTILE_COORD = {2,0};
    void notice(const char * fmt, ...) ;

    void init(HostBlock & hb, TransportBlock & tb) ;
    bool updateTransportBlock() ;
    bool allClear() ;
    bool ship(LogCarStorage::LogCar & lc, u32 carnum) ;
    bool ship(EWCarStorage::EWCar & lc, u32 carnum) ;
    s32 initiateWrite(u32 * data, u32 wordCount, U16C xy, u64 destaddr) ;
    s32 initiateWriteToHost(u32 * data, u32 wordCount, u64 destaddr) {
      //// NOTE WE ARE NOT DEALING WITH THE PCIe TRANSACTION
      //// ATTRIBUTES STUFF IN THE TOP SIX BITS OF destaddr! WHATEVER
      //// THE CALLER PROVIDES WE JUST GO WITH.
      return initiateWrite(data, wordCount, PCIeTILE_COORD, destaddr);
    }

    const HostBlock * mHostBlockPtr;
    TransportBlock * mTransportBlockPtr;

    typedef P2PElevatorPlatform<LogCarStorage::LogCar,T6ElevatorTransport> P2PLogCarManager;
    P2PLogCarManager mP2PLogCarManager;

    typedef P2PElevatorPlatform<EWCarStorage::EWCar,T6ElevatorTransport> P2PEWCarManager;
    P2PEWCarManager mP2PEWCarManager;

  };

  extern T6ElevatorTransport theT6ElevatorTransport;
}


