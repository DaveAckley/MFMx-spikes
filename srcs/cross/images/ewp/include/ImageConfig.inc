#include "EwpBlock.h"
#include "EP_LogBlock.h" // for theLogBlockL1Data

namespace MFM {
XIC_START_IMAGE_BLOCK(EWP)
  XIC_BLOCKADDR(BC_EWPCARS,1,theEwpL1Data.mTheTCStorages, EwpBlockStg)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_LOGBLOCK,1,theLogBlockL1Data.mTheTCStorages,LogBlockStg)
XIC_END_IMAGE_BLOCK(EWP)
}
