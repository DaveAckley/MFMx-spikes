#pragma once   /* -*- C++ -*- */

#include "itype.h"

// Utilities declared in shared/ but implemented separately in host/
// (XUtilsHost.h) and cross/ (XUtilsCross.h)

namespace MFM {
  struct XPrinter {
    typedef void Putchar(int c, void * ctx /*Printer*/) ;
    Putchar * getPutcharFnPtr() ;
    bool print(const char * str) {
      Putchar * fn = getPutcharFnPtr();
      if (!fn) return false;
      if (str) {
        char ch;
        while ((ch = *str++) != 0)
          fn(ch,0);
      }
      return true;
    }
  };

  extern u32 createBits(u8 bitcount) ;

  extern XPrinter DEVNULL;

  extern u32 millisElapsed();

  extern void memset_s(void*, u8, u32) ;
  
}
