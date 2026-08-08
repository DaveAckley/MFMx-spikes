#include "AllImageBlockDecls.h"
#include "ImageConfig.h"
#include "utils.h" // for xstr, conc

namespace MFM {
  /// BEGIN: CHECK KNOWN STRUCTURE SIZES
#define XIC_START_IMAGE_BLOCK(IMAGE_NAME) // empty
#define XIC_BLOCKADDR(BLOCK_CODE, ARRAY_LEN, GLOBAL_ARRAY, VAR_TYPE)    \
  static_assert(CONC(TOTAL_SIZE_,BLOCK_CODE) == (sizeof(GLOBAL_ARRAY[0])*(ARRAY_LEN)), \
                "SIZE CHECK FAILED FOR " XSTR_MACRO(CONC(TOTAL_SIZE_,BLOCK_CODE)) \
                " vs " XSTR_MACRO(sizeof(GLOBAL_ARRAY[0])*(ARRAY_LEN)));
#define XIC_END_IMAGE_BLOCK(IMAGE_NAME) // empty

#include "ImageConfig.inc" // in per-image subdir

#undef XIC_START_IMAGE_BLOCK
#undef XIC_BLOCKADDR
#undef XIC_END_IMAGE_BLOCK
  /// END: CHECK KNOWN STRUCTURE SIZES

  /// BEGIN: DEFINE SUBCLASS 
#define XIC_START_IMAGE_BLOCK(IMAGE_NAME) \
  IMAGE_NAME##ImageBlock theImageBlock __attribute__ ((section(".imageblock"))) = { \
    { /* ImageBlockHeader */                                            \
      .mIBMagic = ImageBlockHeader::IBMAGIC,                            \
      .mImageCode = ImageCode::IC_##IMAGE_NAME,                         \
      .mEntries = IMAGE_NAME##Entries,                                  \
      .mIBCheck = (ImageCode::IC_##IMAGE_NAME)^(IMAGE_NAME##Entries<<2u), \
    },                                                                  \

#define XIC_BLOCKADDR(BLOCK_CODE, STORAGE_COUNT, GLOBAL_STORAGE, VAR_TYPE) \
    { /* ImageBlockAddr m##BLOCK_CODE */                                \
    .mIBAMagic = ImageBlockAddr::IBA_MAGIC,                             \
    .mBlockCode = BlockCode::BLOCK_CODE,                                \
    .mStorageCount = STORAGE_COUNT,                                     \
    .mHostChunkOffsetOpt = U8_MAX, /* assume no host mapping */         \
    .mBlockAddr = (u32) (void*) &(GLOBAL_STORAGE[0]),                   \
  },

#define XIC_END_IMAGE_BLOCK(IMAGE_NAME) }; /* IMAGE_NAME##Entries */    \

#include "ImageConfig.inc" // in per-image subdir

#undef XIC_START_IMAGE_BLOCK
#undef XIC_BLOCKADDR
#undef XIC_END_IMAGE_BLOCK
  /// END: DEFINE SUBCLASS
  
}
