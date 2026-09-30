#pragma once        /* -*- C++ -*- */
#include "HostBlock.h"

namespace MFM {

  void liveTheStandardLife(HostBlock & hb) ;
  bool beatTheStandardHeartbeat(HostBlock & hb) ; // true if beat; for harts with custom main loop (eg HUB/H1)
  void liveTheDefaultStandardLife(HostBlock & hb) ; // all others just dish to this

  extern void stepB(HostBlock & hb) ;
  extern void stepT0(HostBlock & hb) ;
  extern void stepT1(HostBlock & hb) ;
  extern void stepT2(HostBlock & hb) ;
  extern void stepNC(HostBlock & hb) ;
}
