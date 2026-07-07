#pragma once        /* -*- C++ -*- */

#include "HostBlock.h"
//#include "T6ElevatorTransport.h"

namespace MFM {
  extern int hartMainNC(HostBlock & hb) ;
  u64 recordBytesOINC(bool out, u32 count) ;
}
