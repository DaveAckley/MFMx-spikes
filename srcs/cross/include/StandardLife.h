#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {

  extern int liveTheStandardLife(HostBlock & hb);

  extern void stepB(HostBlock & hb);
  extern void stepT0(HostBlock & hb);
  extern void stepT1(HostBlock & hb);
  extern void stepT2(HostBlock & hb);
  extern void stepNC(HostBlock & hb);
}
