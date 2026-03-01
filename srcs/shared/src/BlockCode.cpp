#include "BlockCode.h"
#include "AllImageBlockDecls.h"

namespace MFM {

  extern int strcmp_s(const char * s1, const char * s2) ;

  const char * getNameFromImageCode(ImageCode ic) {

#define XX(NAME)                                \
  case IC_##NAME: return "IC_"#NAME;

    switch (ic) {
    default: break;

    IMAGECODE_LIST()
    }
    return "<illegal ImageCode>";

#undef XX
    
  }

  ImageCode getImageCodeFromName(const char * n) {

#define XX(NAME)                                        \
  if (!strcmp_s(n,"IC_"#NAME)) return IC_##NAME;
    
  IMAGECODE_LIST()
  return IC_RSRV_ILL;

#undef XX

  }


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

  static const u32 blockCodeSizes[] = {
    0u,
#define XX(NAME,TYPE) sizeof(TYPE),
    BLOCKCODE_LIST()
#undef XX
  };

  u32 getSizeFromBlockCode(BlockCode b) {
    MFM_API_ASSERT(b < sizeof(blockCodeSizes)/sizeof(blockCodeSizes[0]), ILLEGAL_ARGUMENT);
    return blockCodeSizes[b];
  }


}
