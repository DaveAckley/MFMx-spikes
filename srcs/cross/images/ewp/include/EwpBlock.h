#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCStorage.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "PT_Ewp.h" // for EwpPayload and EwpBlock
#include "EP_Ewp.h" // for EwpBlockStg and EwpEP

namespace MFM {

  extern T6EPL1Data<EwpBlockStg,1> theEwpL1Data;

}

