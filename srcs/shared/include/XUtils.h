#pragma once   /* -*- C++ -*- */

#include "itype.h"

// Utilities declared in shared/ but implemented separately in host/
// (XUtilsHost.h) and cross/ (XUtilsCross.h)

namespace MFM {
  struct XPrinter {
    typedef void Putchar(int c, void * ctx /*Printer*/) ;
    Putchar * getPutcharFnPtr() ;
  };

  extern u32 millisElapsed();
}
