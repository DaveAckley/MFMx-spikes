#include "ZotBlock.h"
namespace MFM {
XIC_START_IMAGE_BLOCK(ZOT)
// XIC_BLOCKADDR(BC_LOGCARS,1,theLogCarStorage,LogCarStorage)
// XIC_BLOCKADDR(BC_EWCARS,1,theEWCarStorage,EWCarStorage)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_ZOTBLOCK,2,theZotBlockL1Data.mTheTCBlocks,ZotBlock)
XIC_END_IMAGE_BLOCK(ZOT)
}

