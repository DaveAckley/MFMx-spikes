#pragma once /* -*- C++ -*- */

#include <itype.h>
#include <cstdio>
#include <string>
#include <string.h>

#include "BHTag.h"
#include "TimeDefs.h"

#define KTprintf(key, ...) KTEEprintf(key, __FILE__,__LINE__, __VA_ARGS__)
#define Eprintf(...) EEprintf(__FILE__,__LINE__, __VA_ARGS__)
#define Evprintf(...) EEvprintf(__FILE__,__LINE__, __VA_ARGS__)
namespace MFM {
  inline void memset_s(void* addr, u8 byte, u32 count) {
    if (byte == 0u) explicit_bzero(addr,count);
    else memset(addr,byte,count);
  }

  void initHostUtils() ; //< CALL ONCE, VERY EARLY..

  u32 millisElapsed() ;
  double runTimeSeconds() ;

  std::string size4(u64 amt);
  std::string pct4(u64 num, u64 den) ;
  std::string toHex(u64 num) ;

  void interpretFailBits(u8 failbits, u8 * data, u32 count) ;
  void sleepUsec(u32 usec) ;

  u32 millisElapsed() ;
  std::string dateTimeStamp() ;
  void KTEEwrite(const BHTag & key, const u8 * bytes, u32 len) ;
  void KTEEprintf(const BHTag & key, const char * file, u32 line, const char * fmt, ...) ; // fprintf to per-tile host logfile in /tmp
  void EEprintf(const char * file, u32 line, const char * fmt, ...) ; // fprintf to host logfile in /tmp
  void EEvprintf(const char * file, u32 line, const char * fmt, va_list args) ; // vfprintf "
  FILE * getHostLog() ;
  FILE * getHostLogForKey(const BHTag & key) ; // CALLER CLOSES RETURNED FILE *

  template <typename... Args>
  auto stringFormat(std::string_view format, Args&&... args) -> std::string {
    const auto size_s = std::snprintf(nullptr, 0, format.data(), std::forward<Args>(args)...);

    if (size_s <= 0) {
      return {};
    }

    const auto size = static_cast<size_t>(size_s);
    std::string buf;
    buf.resize(size + 1u);
    buf.resize(std::snprintf(buf.data(), buf.size(), format.data(), std::forward<Args>(args)...));

    return buf;
  }

  std::string demangleCpp(const char* typeName) ; //< demangleCpp(typeid(FOO).name())) => MFM::FOO

}

