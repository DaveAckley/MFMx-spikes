#include "AllImageBlockDecls.h"
#include "ImageConfig.h"

namespace MFM {
  /// BEGIN: DEFINE SUBCLASS 
#define XIC_START_IMAGE_BLOCK(IMAGE_NAME) \
  IMAGE_NAME##ImageBlock theImageBlock __attribute__ ((section(".imageblock"))) = { \
    { /* ImageBlockHeader */                                            \
      .mIBMagic = ImageBlockHeader::IBMAGIC,                            \
      .mImageCode = ImageCode::IC_##IMAGE_NAME,                         \
      .mEntries = IMAGE_NAME##Entries,                                  \
      .mIBCheck = (ImageCode::IC_##IMAGE_NAME)^(IMAGE_NAME##Entries<<2u), \
    },                                                                  \

#define XIC_BLOCKADDR(BLOCK_CODE, ARRAY_LEN, GLOBAL_VAR, VAR_TYPE)      \
    { /* ImageBlockAddr m##BLOCK_CODE */                                \
    .mIBAMagic = ImageBlockAddr::IBA_MAGIC,                             \
    .mBlockCode = BlockCode::BLOCK_CODE,                                \
    .mArrayLength = ARRAY_LEN,                                          \
    .mHostChunkOffsetOpt = U8_MAX, /* assume no host mapping */         \
    .mBlockAddr = (u32) (void*) &(GLOBAL_VAR[0]),                       \
  },

#define XIC_END_IMAGE_BLOCK(IMAGE_NAME) }; /* IMAGE_NAME##Entries */    \

#include "ImageConfig.inc" // in per-image subdir

#undef XIC_START_IMAGE_BLOCK
#undef XIC_BLOCKADDR
#undef XIC_END_IMAGE_BLOCK
  /// END: DEFINE SUBCLASS
  
}
