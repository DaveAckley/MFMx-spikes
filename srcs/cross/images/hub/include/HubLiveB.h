#pragma once    /* -*- C++ -*- */
#include "Grid.h"
#include "InterHub.h"

namespace MFM {
  struct FastB {
    GridManager mGridManager;
    InterHubPrivateControl mPIHControl;
  };
  extern FastB fB;
}
