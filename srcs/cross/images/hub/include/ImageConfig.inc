#include "EwpBlock.h"
#include "InterHub.h"
#include "T6Grid.h"

namespace MFM {
XIC_START_IMAGE_BLOCK(HUB)
// XIC_BLOCKADDR(BC_LOGCARS,1,theLogCarStorage,LogCarStorage)
  XIC_BLOCKADDR(BC_EWHUB, 8, theEwpL1Data.mTheTCStorages, EwpBlockStg)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_INTERHUB,4,theInterHubL1Data.mTheTCStorages,InterHubStorage)
  XIC_BLOCKADDR(BC_T6GRID,1,theT6Grid,T6Grid)
XIC_END_IMAGE_BLOCK(HUB)
}
