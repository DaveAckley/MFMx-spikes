#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "HostBlock.h"
#include "AtomicLock.h"
#include "P2PLogElevatorPlatform.h"
#include "T6ElevatorTransport.h"
#include "BaseCar.h"

namespace MFM {
  class Printer {
  public:
    typedef void Putchar(int c, void * ctx /*Printer*/) ;
    void init(AtomicLock & lockWeNeed, P2PLogElevatorPlatform * platform, Putchar *fp, u64 hostaddr) {
      mPrintLockPtr = &lockWeNeed; // Non-zero
      mPlatformPtr = platform;     // Might be zero
      mPutc = fp;                  // Non-zero
      mHostAddr = hostaddr;
    }
    P2PLogElevatorPlatform & getPlatform() ;
    u32 vprintf(const char * format, va_list ap);
    u32 printf(const char * format, ...);
    u64 getHostAddr() const { return mHostAddr; }
    Printer & print(BaseCar<EWBlock> & bc) {
      printf("<EWCar 0x%p c=%u e=%u s=%u\n",
             &bc,
             bc.isComplete(),
             bc.isEmpty(),
             bc.getCarState());
      return *this;
    }
                    
  private:
    AtomicLock * mPrintLockPtr;
    P2PLogElevatorPlatform * mPlatformPtr;
    u64 mHostAddr;
    Putchar * mPutc;
  };

  extern s32 snprintf(char * buf, u32 siz, const char * format, ...) ;
  extern void t6InitPrinters(HostBlock & hb, T6ElevatorTransport & t6t) ;
  extern Printer DP;
  extern Printer LOG;
}
