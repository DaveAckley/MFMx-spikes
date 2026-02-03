#include "BlockCode.h"

namespace MFM {
  
  extern int strcmp_s(const char * s1, const char * s2) ;

  const char * getNameFromBlockCode(BlockCode b) {

#define XX(NAME,TYPE)                           \
  case BC_##NAME: return "BC_"#NAME;

    switch (b) {
    default: break;

    BLOCKCODE_LIST()
    }
    return "<illegal BlockCode>";

#undef XX
    
  }

  BlockCode getBlockCodeFromName(const char * n) {

#define XX(NAME,TYPE)                           \
  if (!strcmp_s(n,"BC_"#NAME)) return BC_##NAME;
    
  BLOCKCODE_LIST()
  return BC_RSRV_ILL;

#undef XX

  }
}
