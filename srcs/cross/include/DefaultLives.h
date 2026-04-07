#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {
  extern int liveB(HostBlock & hb);
  extern int liveT0(HostBlock & hb);
  extern int liveT1(HostBlock & hb);
  extern int liveT2(HostBlock & hb);
  extern int liveNC(HostBlock & hb);

  extern int stepB(HostBlock & hb);
  extern int stepT0(HostBlock & hb);
  extern int stepT1(HostBlock & hb);
  extern int stepT2(HostBlock & hb);
  extern int stepNC(HostBlock & hb);
}
