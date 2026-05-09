#include "ImageCode.h"
#include "AllImageBlockDecls.h"

namespace MFM {

  extern int strcmp_s(const char * s1, const char * s2) ;

  const char * getNameFromImageCode(ImageCode ic) {

#define XX(NAME)                                \
    case CONC(IC_,NAME): return XSTR_MACRO(CONC(IC_,NAME));

    switch (ic) {
    default: break;

    IMAGECODE_LIST()
    }
    return "<illegal ImageCode>";

#undef XX
    
  }

  ImageCode getImageCodeFromName(const char * n) {

#define XX(NAME)                                        \
  if (!strcmp_s(n,XSTR_MACRO(CONC(IC_,NAME)))) return CONC(IC_,NAME);
    
  IMAGECODE_LIST()
  return IC_RSRV_ILL;

#undef XX

  }
}




