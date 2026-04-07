#pragma once  /* -*- C++ -*- */

/* USES 'strcmp_s'
   HOST:   #include "HostUtils.h"
   CROSS:  #include "CrossUtils.h"

   before: #include "BlockCode.h"
*/

#include "itype.h"
#include "CellBlock.h" // everybody needs CellBlock

namespace MFM {
#define IMAGECODE_LIST()                     \
  XX(EWP)                                    \
  XX(HUB)                                    \
  XX(HUB3X3)                                 \
  XX(DEBUG)                                  \
  XX(ZOT)                                    \

  enum ImageCode {
    IC_RSRV_ILL = 0,

#define XX(NAME) IC_##NAME,
    IMAGECODE_LIST()
#undef XX
  };

  const char * getNameFromImageCode(ImageCode b) ;
  ImageCode getImageCodeFromName(const char * n) ;

#define BLOCKCODE_LIST()                        \
  XX(CELLBLOCK,sizeof(CellBlock),CellBlock,1u)  \
  XX(ZOTBLOCK,256*2,ZotBlockStg,2u)             \
  XX(EWHUB,2048*8,EwpBlockStg,8u)               \
  XX(EWPCARS,2048*1,EwpBlockStg,1u)             \
  XX(INTERHUB,8320*4,InterHubStorage,4u)        \
  //END OF BLOCK_CODE_LIST

#define XX(NAME,SIZE,TYPE,BLOCKS)               \
  static constexpr u32 SIZE_BC_##NAME = (SIZE);
    BLOCKCODE_LIST()
#undef XX

#define XX(NAME,SIZE,TYPE,BLOCKS)               \
  BC_##NAME,
  
  enum BlockCode {
    BC_RSRV_ILL = 0,
    BLOCKCODE_LIST()
    BC_BLOCKCODE_COUNT
  };

#undef XX

  u32 getSizeFromBlockCode(BlockCode b) ;
  const char * getNameFromBlockCode(BlockCode b) ;
  BlockCode getBlockCodeFromName(const char * n) ;
}

