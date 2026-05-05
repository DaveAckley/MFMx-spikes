#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCStorage.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "SharedTCs.h" // for InterHubPayload, InterHubBlock, InterHubStorage
#include "SharedEPs.h" // for InterHubEP

namespace MFM {

  extern T6EPL1Data<InterHubStorage,4> theInterHubL1Data;

  //  extern InterHubStorage theInterHubStorage[4];

}

