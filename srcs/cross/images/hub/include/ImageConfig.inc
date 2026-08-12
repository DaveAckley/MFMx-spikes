// THIS FILE IS INCLUDED (MULTIPLE TIMES) BY ImageConfig.h AND ImageConfig.cpp

} // ugh get outside of ImageConfig.h's namespace MFM

#include "EwpBlock.h"
#include "InterHub.h"
#include "T6Grid.h"
#include "EP_LogBlock.h" // for theLogBlockL1Data
#include "EP_ACacheBlock.h" // for theACacheBlockL1Data

namespace MFM { // ugh get back into MFM
extern u32 lastPHASER[1];

XIC_START_IMAGE_BLOCK(HUB)
  XIC_BLOCKADDR(BC_PHASER, 1, lastPHASER, u32)
  XIC_BLOCKADDR(BC_EWHUB, 8, theEwpL1Data.mTheTCStorages, EwpBlockStg)
  XIC_BLOCKADDR(BC_CELLBLOCK,1,theCellBlock,CellBlock)
  XIC_BLOCKADDR(BC_INTERHUB,4,theInterHubL1Data.mTheTCStorages,InterHubStorage)
  XIC_BLOCKADDR(BC_T6GRID,1,theT6Grid,T6Grid)
  XIC_BLOCKADDR(BC_LOGBLOCK,1,theLogBlockL1Data.mTheTCStorages,LogBlockStg)
  XIC_BLOCKADDR(BC_ACACHEBLOCK,1,theACacheBlockL1Data.mTheTCStorages,ACacheBlockStg)
XIC_END_IMAGE_BLOCK(HUB)

#define XIC_IMAGE_TASKS                        \
  /* BEG BO0 BO1 GR0 GR1 GR2 GR3 LIV TASK */   \
  /* no extra tasks in hub init */


