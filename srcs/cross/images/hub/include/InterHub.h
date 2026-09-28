#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCStorage.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "PT_InterHub.h" // for InterHubPayload, InterHubBlock, InterHubStorage
#include "EP_InterHub.h" // for InterHubEP

namespace MFM {

  extern T6EPL1Data<InterHubStorage,8> theInterHubL1Data;

  extern T6EPL1Data<InterHubStorage,4> theCornerHubL1Data;

}

