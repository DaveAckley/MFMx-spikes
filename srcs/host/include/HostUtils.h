#pragma once /* -*- C++ -*- */

#include <itype.h>
#include <cstdio>
#include <string>
#include <string.h>

#define Eprintf(...) EEprintf(__FILE__,__LINE__, __VA_ARGS__)
#define Evprintf(...) EEvprintf(__FILE__,__LINE__, __VA_ARGS__)
namespace MFM {
  inline void memset_s(void* addr, u8 byte, u32 count) {
    if (byte == 0u) explicit_bzero(addr,count);
    else memset(addr,byte,count);
  }
  void interpretFailBits(u8 failbits, u8 * data, u32 count) ;
  void sleepUsec(u32 usec) ;
  void initHostClock() ;
  u32 millisElapsed() ;
  std::string dateTimeStamp() ;
  void EEprintf(const char * file, u32 line, const char * fmt, ...) ; // fprintf to host logfile in /tmp
  void EEvprintf(const char * file, u32 line, const char * fmt, va_list args) ; // vfprintf "
  FILE * getHostLog() ;
}

