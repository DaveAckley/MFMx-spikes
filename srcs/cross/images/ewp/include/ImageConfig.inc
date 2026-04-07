#include "EwpBlock.h"
namespace MFM {
XIC_START_IMAGE_BLOCK(EWP)
// XIC_BLOCKADDR(BC_LOGCARS,1,theLogCarStorage,LogCarStorage)
  XIC_BLOCKADDR(BC_EWPCARS,1,theEwpL1Data.mTheTCBlocks, EwpBlockStg)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
//  XIC_BLOCKADDR(BC_ZOTBLOCK,2,theZotBlockCarsIO,ZotBlock)
XIC_END_IMAGE_BLOCK(EWP)
}
