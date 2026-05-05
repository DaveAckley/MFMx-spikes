#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCStorage.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "SharedTCs.h" // for EwpPayload and EwpBlock
#include "SharedEPs.h" // for EwpBlockStg and EwpEP

namespace MFM {

  extern T6EPL1Data<EwpBlockStg,1> theEwpL1Data;

}

