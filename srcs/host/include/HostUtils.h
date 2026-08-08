#pragma once /* -*- C++ -*- */

#include <itype.h>
#include <cstdio>
#include <string.h>
#include <fstream>

#include "BHTag.h"
#include "TimeDefs.h"

#define H1printf(...) HNprintf(1, __VA_ARGS__)
#define HNprintf(COUNTCN, ...)                                          \
  do {                                                                  \
  static u32 __var;                                                     \
  if (__var++ < COUNTCN)                                                \
    KTEEprintf(BH_HOST_TAG, __FILE__,__LINE__, __VA_ARGS__);            \
  if (__var == COUNTCN)                                                 \
    KTEEprintf(BH_HOST_TAG, __FILE__,__LINE__, "MUTING AFTER %d...\n",__var); \
  } while (0)
#define HTprintf(...) KTEEprintf(BH_HOST_TAG, __FILE__,__LINE__, __VA_ARGS__)
#define KTprintf(key, ...) KTEEprintf(key, __FILE__,__LINE__, __VA_ARGS__)
#define Eprintf(...) EEprintf(__FILE__,__LINE__, __VA_ARGS__)
#define Evprintf(...) EEvprintf(__FILE__,__LINE__, __VA_ARGS__)
namespace MFM {
  inline void memoryFence() { /* empty */ }
  inline void writeCommit32L1(u32* address, u32 newvalue) { *address = newvalue; }
  inline void writeCommit16L1(u16* address, u16 newvalue) { *address = newvalue; }
  inline void writeCommit8BL1(u8* address, u8 newvalue) { *address = newvalue; }

  static constexpr BHTag BH_HOST_TAG(TagType::HOSTCT, 0u);
  inline void memset_s(void* addr, u8 byte, u32 count) {
    if (byte == 0u) explicit_bzero(addr,count);
    else memset(addr,byte,count);
  }

  u8 p1(void * p) ;
  u16 p2(void * p) ;
  u32 p4(void * p) ;
  u64 p8(void * p) ;

  int strcmp_s(const char * s1, const char * s2) ;

  void initHostUtils() ; //< CALL ONCE, VERY EARLY..

  u32 millisElapsed() ;
  double secondsSinceStart(TimeStamp tothis) ;
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
  std::ofstream getOStreamLogForKey(const BHTag & key) ; // CALLER CLOSES RETURNED FILE *

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

  std::string execShellCmd(const std::string& cmd) ;

  std::string makeMark(std::string fidl, u32 dev, U8C noc0, std::string hartname, std::string msg) ;
}

