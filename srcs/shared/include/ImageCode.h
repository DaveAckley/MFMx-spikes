#pragma once  /* -*- C++ -*- */

/* USES 'strcmp_s'
   HOST:   #include "HostUtils.h"
   CROSS:  #include "CrossUtils.h"

   before: #include "ImageCode.h"
*/

#include "itype.h"

namespace MFM {
#define IMAGECODE_LIST()                     \
  XX(EWP)                                    \
  XX(HUB)                                    \
  XX(HUB3X3)                                 \
  XX(DEBUG)                                  \
  XX(ZOT)                                    \

  enum ImageCode : u8 {
    IC_RSRV_ILL = 0,

#define XX(NAME) IC_##NAME,
    IMAGECODE_LIST()
#undef XX
  };

  const char * getNameFromImageCode(ImageCode b) ;
  ImageCode getImageCodeFromName(const char * n) ;
}


