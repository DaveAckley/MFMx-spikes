#pragma once  /* -*- C++ -*- */

/* USES 'strcmp_s'
   HOST:   #include "HostUtils.h"
   CROSS:  #include "CrossUtils.h"

   before: #include "BlockCode.h"
*/

#include "itype.h"
#include "CellBlock.h" // everybody needs CellBlock

namespace MFM {

#define BLOCKCODE_LIST()                        \
  XX(CELLBLOCK,CLBK,sizeof(CellBlock),CellBlock,1u)     \
  XX(LOGBLOCK,LGBK,4096,LogBlockStg,1u)                 \
  XX(ZOTBLOCK,ZTBK,256,ZotBlockStg,2u)                  \
  XX(EWHUB,EWHB,2048,EwpBlockStg,8u)                    \
  XX(EWPCARS,EWCR,2048,EwpBlockStg,1u)                  \
  XX(INTERHUB,IHUB,8320,InterHubStorage,4u)             \
  XX(T6GRID,T6G,178852,T6Grid,1u)                       \
  XX(ACACHEBLOCK,ACB,3072,ACacheBlockStg,1u)            \
  //END OF BLOCK_CODE_LIST

#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT)                   \
  static constexpr u32 STORAGE_SIZE_BC_##NAME = (STORAGE_SIZE);         \
  static constexpr u32 STORAGE_COUNT_BC_##NAME = (STORAGE_COUNT);       \
  static constexpr u32 TOTAL_SIZE_BC_##NAME = STORAGE_SIZE_BC_##NAME * STORAGE_COUNT_BC_##NAME;
    BLOCKCODE_LIST()
#undef XX

#define XX(NAME,ABBR,STORAGE_SIZE,TYPE,STORAGE_COUNT)   \
  BC_##NAME,
  
  enum BlockCode : u8 {
    BC_RSRV_ILL = 0,
    BLOCKCODE_LIST()
    BC_BLOCKCODE_COUNT
  };

#undef XX

  u32 getStorageSizeFromBlockCode(BlockCode b) ;
  u32 getStorageCountFromBlockCode(BlockCode b) ;
  const char * getNameFromBlockCode(BlockCode b) ;
  const char * getAbbrFromBlockCode(BlockCode b) ;
  BlockCode getBlockCodeFromName(const char * n) ;
}

