#pragma once /* -*- C++ -*- */

#include <itype.h>
#include <cstdio>

#define Eprintf(...) EEprintf(__FILE__,__LINE__, __VA_ARGS__)
namespace MFM {
  void sleepUsec(u32 usec) ;
  void EEprintf(const char * file, u32 line, const char * fmt, ...) ; // fprintf to host logfile in /tmp
  FILE * getHostLog() ;
}

