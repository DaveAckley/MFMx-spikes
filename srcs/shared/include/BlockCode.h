#pragma once  /* -*- C++ -*- */

#include "itype.h"

namespace MFM {
  enum ImageCode {
    IC_RSRVILL = 0,
    IC_EWP = 1,                //< Generic event Window processor
    IC_HUB = 2,                //< 2x2 w/hub@00 + 3 EWPs @01,10,11
    IC_HUB3X3 = 3,             //< 3x3 w/hub@11 + 8 EWPs
    IC_DEBUG = 4,              //< unspecified test images
  };

  enum BlockCode {
    BC_RSRV_ILL = 0,
    BC_LOGCARS,                 //< LogCarStorage theLogCarStorage
    BC_EWCARS,                  //< EWCarStorage theEWCarStorage
  };
}

