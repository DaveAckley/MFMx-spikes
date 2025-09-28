#ifndef P2PELEVATOR_H    /* -*- C++ -*- */
#define P2PELEVATOR_H

#include "itype.h"

namespace MFM {
  class AbstractCar {
  public:
    bool readyToDepart() ;
    void departed() ;
    void arrived() ;
  };

  class ElevatorTransport {
  public:
    void update(AbstractCar & ac) ;
  };

  // Reqmts:
  //  CAR has a default ctor
  //  BANKS > 0
  template <class CAR, u32 BANKS>
  class P2PElevatorPlatform {
  public:
    P2PElevatorPlatform(bool cars) ;
    bool update(ElevatorTransport & et) ;
    enum PayloadState : u8 {
      PS_ARRIVED,
      PS_LOADING,
      PS_DEPARTED,
    };
    struct Payload {
      u64 mSenderAddress;
      CAR mCar;
      PayloadState mPayloadState;
    };

  private:
    Payload mBank[BANKS];
    u32 mCurrentBank;
  };

  class LogCar {
    u8 mByteCount;
    u8 mBytes[1<<8];
  };

  //  typedef P2PElevatorPlatform<LogElevator,3> LogElevatorPlatform;

  template <class CAR>
  class P2PElevator {
  public:

    virtual void sendCar(bool source, CAR &) = 0;
    virtual ~P2PElevator() { }
  private:
  };
}

#include "P2PElevator.tcc"

#endif /* P2PELEVATOR_H */
