#include "ZotBlock.h"
#include "EP_LogBlock.h" // for theLogBlockL1Data

namespace MFM {
XIC_START_IMAGE_BLOCK(ZOT)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_LOGBLOCK,1,theLogBlockL1Data.mTheTCStorages,LogBlockStg)
  XIC_BLOCKADDR(BC_ZOTBLOCK,2,theZotBlockL1Data.mTheTCStorages,ZotBlock)
XIC_END_IMAGE_BLOCK(ZOT)
}

