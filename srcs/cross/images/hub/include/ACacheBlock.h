#pragma once /* -*- C++ -*- */
#include "TC.h"
#include "T6EPs.h"
#include "TCStorage.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "AtomicLock.h"
#include "BlockCode.h"
#include "PT_ACacheBlock.h" // for ACacheBlockPayload, ACacheBlockBlock, ACacheBlockStorage
#include "EP_ACacheBlock.h" // for ACacheBlockEP

namespace MFM {

  extern T6EPL1Data<ACacheBlockStg,1> theACacheBlockL1Data;
  extern ACacheBlockL1Control theACacheBlockL1Control;

  //  extern ACacheBlockStorage theACacheBlockStorage[4];

}

