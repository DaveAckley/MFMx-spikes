#pragma once  /* -*- C++ -*- */

/* USES 'strcmp_s'
   HOST:   #include "HostUtils.h"
   CROSS:  #include "CrossUtils.h"

   before: #include "BlockCode.h"
*/

#include "itype.h"

namespace MFM {
  enum ImageCode {
    IC_RSRVILL = 0,
    IC_EWP = 1,                //< Generic event Window processor
    IC_HUB = 2,                //< 2x2 w/hub@00 + 3 EWPs @01,10,11
    IC_HUB3X3 = 3,             //< 3x3 w/hub@11 + 8 EWPs
    IC_DEBUG = 4,              //< unspecified test images
    IC_ZOT = 5,                //< another scratch image
  };

#define BLOCKCODE_LIST()                     \
  XX(LOGCARS,LogCarStorage)                  \
  XX(EWCARS,EWCarStorage)                    \
  XX(CELLBLOCK,CellBlock)                    \
  XX(EWHUB,EWHub[8])

#define XX(NAME,TYPE) \
  BC_##NAME,
  
  enum BlockCode {
    BC_RSRV_ILL = 0,
    BLOCKCODE_LIST()
    BC_BLOCKCODE_COUNT
  };

#undef XX

  const char * getNameFromBlockCode(BlockCode b) ;
  BlockCode getBlockCodeFromName(const char * n) ;
}

