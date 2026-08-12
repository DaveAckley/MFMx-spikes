#pragma once    /* -*- C++ -*- */

#include "itype.h"

#include "CrossUtils.h" // for strcmp

#include "ImageBlock.h"

namespace MFM {

  /////
  /// BEGIN: COUNT ENTRIES
#define XIC_START_IMAGE_BLOCK(IMAGE_NAME) static constexpr u32 IMAGE_NAME##Entries = 
#define XIC_BLOCKADDR(BLOCK_CODE, ARRAY_LEN, GLOBAL_VAR, VAR_TYPE) +1
#define XIC_END_IMAGE_BLOCK(IMAGE_NAME) ; /* IMAGE_NAME##Entries */

#include "ImageConfig.inc" // in per-image subdir

#undef XIC_START_IMAGE_BLOCK
#undef XIC_BLOCKADDR
#undef XIC_END_IMAGE_BLOCK
  /// END: COUNT ENTRIES
  /////

  /////
  /// BEGIN: DECLARE SUBCLASS
#define XIC_START_IMAGE_BLOCK(IMAGE_NAME) struct IMAGE_NAME##ImageBlock : public ImageBlockHeader {
#define XIC_BLOCKADDR(BLOCK_CODE, ARRAY_LEN, GLOBAL_VAR, VAR_TYPE) ImageBlockAddr m##BLOCK_CODE;
#define XIC_END_IMAGE_BLOCK(IMAGE_NAME) } ; /* IMAGE_NAME##Entries */ \
  extern IMAGE_NAME##ImageBlock theImageBlock;

#include "ImageConfig.inc" // in per-image subdir

#undef XIC_START_IMAGE_BLOCK
#undef XIC_BLOCKADDR
#undef XIC_END_IMAGE_BLOCK
  /// END: DECLARE SUBCLASS
  /////

}
