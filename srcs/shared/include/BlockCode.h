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
  XX(CELLBLOCK,sizeof(CellBlock),CellBlock,1u)  \
  XX(LOGBLOCK,3072,LogBlockStg,1u)              \
  XX(ZOTBLOCK,256,ZotBlockStg,2u)               \
  XX(EWHUB,2048,EwpBlockStg,8u)                 \
  XX(EWPCARS,2048,EwpBlockStg,1u)               \
  XX(INTERHUB,8320,InterHubStorage,4u)          \
  XX(T6GRID,178852,T6Grid,1u)                   \
  XX(ACACHEBLOCK,4416,ACacheBlockStg,1u)        \
  //END OF BLOCK_CODE_LIST

#define XX(NAME,STORAGE_SIZE,TYPE,STORAGE_COUNT)                        \
  static constexpr u32 STORAGE_SIZE_BC_##NAME = (STORAGE_SIZE);         \
  static constexpr u32 STORAGE_COUNT_BC_##NAME = (STORAGE_COUNT);       \
  static constexpr u32 TOTAL_SIZE_BC_##NAME = STORAGE_SIZE_BC_##NAME * STORAGE_COUNT_BC_##NAME;
    BLOCKCODE_LIST()
#undef XX

#define XX(NAME,STORAGE_SIZE,TYPE,STORAGE_COUNT)        \
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
  BlockCode getBlockCodeFromName(const char * n) ;
}

