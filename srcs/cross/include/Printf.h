#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "AtomicLock.h"
#include "XUtils.h"
#include "Fail.h"
#include "HostBlock.h"

namespace MFM {
  
  class Printer : public XPrinter {
  public:
    /// XPrinter API
    Putchar * getPutcharFnPtr() {
      MFM_API_ASSERT_NONNULL(mPutc);
      return mPutc;
    }
    void reset() { mPutc = 0; }
    
    typedef void Putchar(int c, void * ctx /*Printer*/) ;

    void init(AtomicLock & lockWeNeed, Putchar *fp) {
      mPrintLockPtr = &lockWeNeed; // Non-zero
      mPutc = fp;                  // Non-zero
    }
    u32 vprintf(const char * format, va_list ap);
    u32 printf(const char * format, ...);
    //u64 getHostAddr() const { return mHostAddr; }
    /*
    Printer & print(BaseCar<EWBlock> & bc) {
      printf("<EWCar 0x%p c=%u e=%u s=%u\n",
             &bc,
             bc.isComplete(),
             bc.isEmpty(),
             bc.getCarState());
      return *this;
    }
    */              
  private:
    AtomicLock * mPrintLockPtr;
    Putchar * mPutc;
  };

  extern s32 snprintf(char * buf, u32 siz, const char * format, ...) ;
  extern void t6InitPrinters(HostBlock & hb) ;
  extern Printer DP;
  extern Printer LOG;
}
