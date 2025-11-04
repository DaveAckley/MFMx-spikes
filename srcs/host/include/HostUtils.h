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

  std::string size4(u64 amt);
  std::string pct4(u64 num, u64 den) ;

  void interpretFailBits(u8 failbits, u8 * data, u32 count) ;
  void sleepUsec(u32 usec) ;
  void initHostClock() ;
  u32 millisElapsed() ;
  std::string dateTimeStamp() ;
  void EEprintf(const char * file, u32 line, const char * fmt, ...) ; // fprintf to host logfile in /tmp
  void EEvprintf(const char * file, u32 line, const char * fmt, va_list args) ; // vfprintf "
  FILE * getHostLog() ;

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
}

