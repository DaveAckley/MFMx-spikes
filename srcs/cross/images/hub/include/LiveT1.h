#pragma once  /* -*- C++ -*- */
#include "RingBuffer.h"
#include "HostBlock.h"
#include "lzmfmx.h"

namespace MFM {
  extern LZBuf l1dLZBytesIn;
  extern LZBuf l1dLZBytesOut;
  extern int initT1(HostBlock & hb) ;
  extern int liveT1(HostBlock & hb) ;
}
