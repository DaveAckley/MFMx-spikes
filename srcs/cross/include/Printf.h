#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include "HostBlock.h"
#include "AtomicLock.h"
#include <stdarg.h>

namespace MFM {
  class Printer {
  public:
    typedef void Putchar(int c, void * ctx) ;
    void init(Putchar *fp, u64 hostaddr) {
      mPutc = fp;
      mHostAddr = hostaddr;
    }
    u32 vprintf(const char * format, va_list ap);
    u32 printf(const char * format, ...);
    u64 getHostAddr() const { return mHostAddr; }
  private:
    u64 mHostAddr;
    Putchar * mPutc;
    AtomicLock mLock;
  };

  extern void t6InitPrinters(HostBlock & hb) ;
  extern Printer DP;
  extern Printer LOG;
}
