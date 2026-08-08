#include "BlockCode.h"

namespace MFM {

  extern int strcmp_s(const char * s1, const char * s2) ;

  const char * getNameFromBlockCode(BlockCode b) {

#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT)           \
  case CONC(BC_,NAME): return XSTR_MACRO(CONC(BC_,NAME));

    switch (b) {
    default: break;

    BLOCKCODE_LIST()
    }
    return "<illegal BlockCode>";

#undef XX
    
  }

  const char * getAbbrFromBlockCode(BlockCode b) {

#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT)           \
  case CONC(BC_,NAME): return XSTR_MACRO(ABBR);

    switch (b) {
    default: break;

    BLOCKCODE_LIST()
    }
    return "<illegabbr>";

#undef XX
    
  }
  
  BlockCode getBlockCodeFromName(const char * n) {

#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT)                   \
  if (!strcmp_s(n,XSTR_MACRO(CONC(BC_,NAME)))) return CONC(BC_,NAME);
    
  BLOCKCODE_LIST()
  return BC_RSRV_ILL;

#undef XX

  }

  static const u32 blockCodeStorageSizes[] = {
    0u,
#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT) (STORAGE_SIZE), // Note we're NOT doing sizeof(TYPE)!
    BLOCKCODE_LIST()
#undef XX
  };

  static const u32 blockCodeStorageCounts[] = {
    0u,
#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT) (STORAGE_COUNT),
    BLOCKCODE_LIST()
#undef XX
  };

  u32 getStorageSizeFromBlockCode(BlockCode b) {
    MFM_API_ASSERT(b < sizeof(blockCodeStorageSizes)/sizeof(blockCodeStorageSizes[0]), ILLEGAL_ARGUMENT);
    return blockCodeStorageSizes[b];
  }

  u32 getStorageCountFromBlockCode(BlockCode b) {
    MFM_API_ASSERT(b < sizeof(blockCodeStorageCounts)/sizeof(blockCodeStorageCounts[0]), ILLEGAL_ARGUMENT);
    return blockCodeStorageCounts[b];
  }


}
