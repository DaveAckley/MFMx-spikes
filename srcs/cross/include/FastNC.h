#pragma once        /* -*- C++ -*- */

#include "HostBlock.h"
//#include "T6ElevatorTransport.h"

namespace MFM {
  extern int hartMainNC(HostBlock & hb);

  typedef bool (*EPFuncPtr)(bool doInit);
}
