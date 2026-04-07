#include "EwpBlock.h"
#include "InterHub.h"

namespace MFM {
XIC_START_IMAGE_BLOCK(HUB)
// XIC_BLOCKADDR(BC_LOGCARS,1,theLogCarStorage,LogCarStorage)
  XIC_BLOCKADDR(BC_EWHUB, 8, theEwpL1Data.mTheTCBlocks, EwpBlockStg)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_INTERHUB,4,theInterHubL1Data.mTheTCBlocks,InterHubStorage)
XIC_END_IMAGE_BLOCK(HUB)
}
