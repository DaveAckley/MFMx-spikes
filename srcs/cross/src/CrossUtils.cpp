#include <stdarg.h>
#include "CrossUtils.h"
#include "Fail.h"
#include "XUtils.h"
#include "HostBlock.h"
#include "nanoprintf.h"
#include "FastLocal.h" // for fAll
#include "AtomicLock.h"
#include "Debug.h"
#include "EP_LogBlock.h" // for theLogBlockL1Control
#include "P4Atom.h"
#include "FastT0.h"             // for ticksElapsed()

namespace MFM {
  DebugHookFncPtr theGlobalDebugHook;

  extern HostBlock theHostBlock;

  static AtomicLock stringPackLock;
  void packToHost(char * buf) { // let's try marking one at a time..
    u32 count;
    for (count = 0; buf[count]; ++count) { }
    MFM_API_ASSERT(count > 1,UNSUPPORTED_SIZE);
    if (count&1) {
      buf[count-2] = buf[count-1];
      buf[count-1] = 0;
    }

    {
      AtomicScopeLock guard(stringPackLock);
      theHostBlock.packString(buf);
    }
  }

  void markLogBlock(u16 fileid, u16 lineno,const char * msg) {
    theLogBlockL1Control.writeMark(fileid, lineno, msg);
  }

  void markLogBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag = 0) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s0x%lx'%08lx.",
                 tag?tag:"=",
                 (u32)(val>>32),
                 (u32)(val&0xffffffff)
                 );
    markLogBlock(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s0x%p", tag?tag:"=", ptr);
    markLogBlock(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const int val, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s%d", tag?tag:"=", val);
    markLogBlock(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const U8C c, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s(%u,%u)", tag?tag:"=", c.x, c.y);
    markLogBlock(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const S8C c, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s(%d,%d)", tag?tag:"=", c.x, c.y);
    markLogBlock(fileid,lineno,buf);
  }
  
  void markLogBlock(u16 fileid, u16 lineno,const S16C c, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s(%d,%d)", tag?tag:"=", c.x, c.y);
    markLogBlock(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const char * msg, const char * tag) {
    constexpr u32 BUF_SIZ = 40;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"%s%s",
                 tag?tag:"",
                 msg
                 );
    markLogBlock(fileid,lineno,buf);
  }

  static u16 lowTicks() { return ticksElapsed()%10'000; }
  static u16 lowMillis() { return millisElapsed()&0xffff; }

  void markHostBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag = 0) {
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s0x%p} \n",
                 fileid,lineno,
                 lowMillis(), //lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 ptr
                 );
    packToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const U8C c, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%u,%u)} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 c.x, c.y
                 );
    packToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const S8C c, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%d,%d)} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 c.x, c.y
                 );
    packToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const S16C c, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%ld,%ld)} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 (s32) c.x, (s32) c.y
                 );
    packToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const int val, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s%d} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 val
                 );
    packToHost(buf);
  }

  void markHostBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s0x%lx'%08lx} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" =",
                 (u32)(val>>32),
                 (u32)(val&0xffffffff)
                 );
    packToHost(buf);
  }
  
  void markHostBlock(u16 fileid, u16 lineno,const char *msg, const char * tag = 0) {
    extern HostBlock theHostBlock;
    constexpr u32 BUF_SIZ = 80;
    char buf[BUF_SIZ];
    npf_snprintf(buf,BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s%s} \n",
                 fileid,lineno,
                 lowMillis(),//lowTicks(),
                 theHostBlock.mChipNum,
                 theHostBlock.mNoC0.x,
                 theHostBlock.mNoC0.y,
                 hartChar(fAll.mHartNum),
                 tag?tag:" ",
                 msg
                 );
    packToHost(buf);
  }

  void memset_s(void* addr, u8 byte, u32 count) {
    memset(addr,byte,count); // don't have explicit_bzero in libs I'm using?
  }

  int strcmp_s(const char *s1, const char *s2) {
    MFM_API_ASSERT(s1 && s2,ILLEGAL_ARGUMENT);
    while (*s1 && *s2) {
      int d = *s1++ - *s2++;
      if (d) return d;
    }
    if (*s1) return 1;
    if (*s2) return -1;
    return 0;
  }

  //extern void XXX_DEBUG_FUNC(const char * file, u32 line) __attribute__ ((used)) ;

  static u8 * getu8fog(u32 p) {
    volatile union { u32 a; u8 * b; } c;
    c.a = p;
    return c.b;
  }
  static u32 * getu32fog(u32 p) {
    volatile union { u32 a; u32 * b; } c;
    c.a = p;
    return c.b;
  }

  const char * stripDirs(const char * path, u32 dircount) {
    const char * suf = path;
    while (*++suf != '\0') { } // get to end
    for (u32 i = 0u; i <= dircount; ++i) {  // keep only dircount dirs to save log space
      while (suf != path && *--suf != '/') { }
    }
    if (!*suf) suf = path;
    return suf;
  }

  void markLogBlock(u16 fileid, u16 lineno,const P4Atom atom) {
    AtomCharBuf buf;
    formatP4Atom(atom,buf);
    markLogBlock(fileid,lineno,buf);
  }

  bool formatP4Atom(const P4Atom a, AtomCharBuf buf) {
    if (!a.isValid()) {
      snprintf(&buf[0],ACBUF_SIZE,"!V0x%04x'%04x'%08x'%08x",
               (u32) a.mParityAndType,
               (u32) a.mData0,
               a.mStg[0], a.mStg[1]);
      return false;
    }      
    snprintf(&buf[0],ACBUF_SIZE,"t%d/%x:0x%04x'%08x'%08x",
             a.getType(), a.getType(),
             (u32) a.mData0,
             a.mStg[0], a.mStg[1]);
    return true;
  }
}
