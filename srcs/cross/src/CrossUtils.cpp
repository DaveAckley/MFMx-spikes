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
#include "PHASER.h" // for PhaserBolt
#include "PT_InterHub.h" // for InterHubBlock
#include "HostBlock.h"

namespace MFM {
  DebugHookFncPtr theGlobalDebugHook;

  static constexpr u32 BIG_BUF_SIZ = 100;
  static constexpr u32 MARK_BUF_SIZ = 64;
  static constexpr u32 MINI_BUF_SIZ = 32;

  extern HostBlock theHostBlock;
  bool logTagCheck(const char * tag) {
    u8 flags = theHostBlock.mHostFlags;
    if (!tag || tag[0] == 0 || tag[0] != '.') // missing, empty, and OG tags are in unless NO_UTAG
      return !(flags&HostFlags::HBF_NO_UTAG);
    // STRUCTURED TAG HANDLING
    if (tag[1] == 'p')          //< ".pSTUF" for packet tags
      return flags&HostFlags::HBF_STAG_PTRACE;
    return true;                //< make unrecognized stags obnoxious
  }


  static AtomicLock stringPackLock;
  static void debugPackToHost(char * buf) { // let's try marking one at a time..
    // Every HB mark ends here: a FAIL in here is the engine's own state being
    // wrong, never a recoverable physics failure (RecoverPoint.h).
    RecoverSuspend noRecovery(fAll.mRecover);
    // Suppress all debug logging after a fail if hostflag is set
    if ((theHostBlock.mHostFlags & HostFlags::HBF_ASSERT_STOP_LOG) != 0 &&
        (theHostBlock.mFails != 0))
      return;

    u32 count;
    const char *abuf;
    for (count = 0; buf[count]; ++count) { }
    if (count <= 1) {
      abuf = "\\/";
      count = 2;
    } else
      abuf = buf;
    MFM_API_ASSERT(count > 1,UNSUPPORTED_SIZE);
    if (count&1) {
      buf[count-2] = buf[count-1];
      buf[count-1] = 0;
    }

    {
      AtomicScopeLock guard(stringPackLock);
      theHostBlock.packString(abuf);
    }
  }

  // Unconditional log attempt
  void markLogBlockDoIt(u16 fileid, u16 lineno,const char * msg) {
    RecoverSuspend noRecovery(fAll.mRecover); // L1 log ring state: see above
    theLogBlockL1Control.writeMark(fileid, lineno, msg);
  }

  void markLogBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag = 0) {
    if (!logTagCheck(tag)) return;
    char buf[MINI_BUF_SIZ];
    snprintf(buf,MINI_BUF_SIZ,"%s0x%lx'%08lx.",
             tag?tag:"=",
             (u32)(val>>32),
             (u32)(val&0xffffffff)
             );
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s0x%p", tag?tag:"=", ptr);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const int val, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s%d", tag?tag:"=", val);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const U8C c, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s(%u,%u)", tag?tag:"=", c.x, c.y);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const U8CRange r, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s[%u,%u<%u,%u]",
             tag?tag:"=",
             r.start.x,r.start.y,
             r.stop.x,r.stop.y);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const U16CRange r, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s[%u,%u<%u,%u)",
             tag?tag:"=",
             r.start.x,r.start.y,
             r.stop.x,r.stop.y);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const S8C c, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s(%d,%d)", tag?tag:"=", c.x, c.y);
    markLogBlockDoIt(fileid,lineno,buf);
  }
  
  void markLogBlock(u16 fileid, u16 lineno,const S16C c, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s(%d,%d)", tag?tag:"=", c.x, c.y);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  void markLogBlock(u16 fileid, u16 lineno,const char * msg, const char * tag) {
    if (!logTagCheck(tag)) return;
    char buf[MARK_BUF_SIZ];
    snprintf(buf,MARK_BUF_SIZ,"%s%s",
             tag?tag:"",
             msg
             );
    markLogBlockDoIt(fileid,lineno,buf);
  }

  static u16 lowTicks() { return ticksElapsed()%10'000; }
  static u16 lowMillis() { return millisElapsed()&0xffff; }

  void markHostBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag = 0) {
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s0x%p} \n",
             fileid,lineno,
             lowMillis(), //lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" =",
             ptr
             );
    debugPackToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const U8C c, const char * tag = 0) {
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%u,%u)} \n",
             fileid,lineno,
             lowMillis(),//lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" =",
             c.x, c.y
             );
    debugPackToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const S8C c, const char * tag = 0) {
    extern HostBlock theHostBlock;
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%d,%d)} \n",
             fileid,lineno,
             lowMillis(),//lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" =",
             c.x, c.y
             );
    debugPackToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const S16C c, const char * tag = 0) {
    extern HostBlock theHostBlock;
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s(%ld,%ld)} \n",
             fileid,lineno,
             lowMillis(),//lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" =",
             (s32) c.x, (s32) c.y
             );
    debugPackToHost(buf);
  }

  void markHostBlock(u16 fileid, u16 lineno,const int val, const char * tag = 0) {
    extern HostBlock theHostBlock;
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s%d} \n",
             fileid,lineno,
             lowMillis(),//lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" =",
             val
             );
    debugPackToHost(buf);
  }

  void markHostBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag = 0) {
    extern HostBlock theHostBlock;
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s0x%lx'%08lx} \n",
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
    debugPackToHost(buf);
  }
  
  void markHostBlock(u16 fileid, u16 lineno,const char *msg, const char * tag = 0) {
    extern HostBlock theHostBlock;
    char buf[BIG_BUF_SIZ];
    snprintf(buf,BIG_BUF_SIZ,"{%u:%u %u %u %d,%d h%c%s%s} \n",
             fileid,lineno,
             lowMillis(),//lowTicks(),
             theHostBlock.mChipNum,
             theHostBlock.mNoC0.x,
             theHostBlock.mNoC0.y,
             hartChar(fAll.mHartNum),
             tag?tag:" ",
             msg
             );
    debugPackToHost(buf);
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
    //XXX perhaps implement: if (!logTagCheck(".a")) return;
    AtomCharBuf buf;
    formatP4Atom(atom,buf);
    markLogBlockDoIt(fileid,lineno,buf);
  }

  bool formatP4Atom(const P4Atom a, AtomCharBuf buf) {
    if (!a.isValid()) {
      snprintf(&buf[0],ACBUF_SIZE,"!V0x%04x'%04x'%08x'%08x",
               (u32) a.mParityAndType,
               (u32) a.mData0,
               a.mStg[0], a.mStg[1]);
      return false;
    }      
    snprintf(&buf[0],ACBUF_SIZE,"#%d:0x%04x'%04x'%08x'%08x",
             a.getType(),
             (u32) a.mParityAndType,
             (u32) a.mData0,
             a.mStg[0], a.mStg[1]);
    return true;
  }
  
  char * formatCountedByte(u32 count, u8 byte) {
    static char buf[16];
    u32 digits = 0;
    for (u32 d = count; d > 0; ++digits) d = d/10;
    snprintf(buf,sizeof(buf),"%d%d 0x%02x",digits,count,byte);
    return buf;
  }

  const char * formatPhaserBolt(const PhaserBolt & pb) {
    static constexpr u32 BOLT_BUFSIZ = 64;
    static char buf[BOLT_BUFSIZ];
    u8 pseq = pb.getSeqNo();
    PhaserBolt::Cmd cmd = pb.getCmd();
    const char * cmdname = PhaserBolt::phaserCmdName(cmd);
    u32 args = PhaserBolt::phaserArgCount(cmd);
    switch (args) {
    case 0:
      snprintf(buf,BOLT_BUFSIZ,"%u.%s!",pseq,cmdname);
      break;
    case 1:
      snprintf(buf,BOLT_BUFSIZ,"%u.%s %d!",pseq,cmdname
               ,pb.getBoltDataWordOrDie(0));
      break;
    case 2:
      snprintf(buf,BOLT_BUFSIZ,"%u.%s %d,%d!",pseq,cmdname
               ,pb.getBoltDataWordOrDie(0)
               ,pb.getBoltDataWordOrDie(1)
               );
      break;
    case 5:
      snprintf(buf,BOLT_BUFSIZ,"%u.%s %d,%d,%d,%d,%d!",pseq,cmdname
               ,pb.getBoltDataWordOrDie(0)
               ,pb.getBoltDataWordOrDie(1)
               ,pb.getBoltDataWordOrDie(2)
               ,pb.getBoltDataWordOrDie(3)
               ,pb.getBoltDataWordOrDie(4)
               );
      break;
    default:
      snprintf(buf,BOLT_BUFSIZ,"%u.%s#%u? you lie",pseq,cmdname,args
               );
      break;
    }
    return buf;
  }

  const char * strTag3(const void * p) {
    static char buf[4];
    u32 tag = ptrTag3(p);
    snprintf(buf,4,"%03x",tag);
    return buf;
  }

  const char * strTag4(const void * p) {
    static char buf[5];
    u32 tag = ptrTag4(p);
    snprintf(buf,5,"%04x",tag);
    return buf;
  }

  const char * strTag4Buf32(Buf32 & b32, const void * p) {
    u32 tag = ptrTag4(p);
    snprintf(b32.buf,sizeof(b32),"%04x",tag);
    return b32.buf;
  }

  const char * hexDump(const void * p) {
    static Buf32 b32;
    return hexDumpBuf32(b32,p);
  }

  const char * hexDumpBuf32(Buf32 & b32, const void * p) {
    memoryFence();
    constexpr u32 WORDS = 2;
    constexpr u32 BUF_SIZ = sizeof(Buf32);
    memset_s(&b32,0,BUF_SIZ);
    u8 * cp = (u8*) p;
    u8 byte = 0;
    bool next = false;
    snprintf(b32.buf,7,"%05x/",0xfffff&(u32)p); // include low-order nyb!
    
    for (u32 i = 6; i < BUF_SIZ-3; ++i) {
      if ((next = !next)) byte = *cp++; 
      else byte = byte<<4;
      u8 nyb = (byte>>4) & 0xf;
      b32.buf[i] = nyb <= 9 ? '0'+nyb : 'a'+nyb-10;
      if ((i % 9) == 4) b32.buf[++i] = ' ';
    }
    return b32.buf;
  }

  const char * formatIHBlock(InterHubBlock & ihb,Dir8 d8, u32 caridx) {
    static Buf32 b32;
    return formatIHBlockBuf32(b32, ihb, d8, caridx);
  }

  // PTRACE mark payload (see scripts/ptrace/README.txt):
  //   send: OUR+SIZE#NN->DEST(x,y)    recv: OUR+SIZE#NN<-DIR
  // NN is the 2-hex TCMarker nonce from the packet header.
  const char * formatT6SendBuf32(Buf32 & b32, void * ouraddr, u32 pktsiz, u8 nonce, U8C noc0, void * theiraddr) {
    snprintf(b32.buf,b32.SIZE,
             "%04x+%u#%02x%s%04x(%u,%u)",
             ptrTag4(ouraddr),
             pktsiz,
             (u32) nonce,
             "->",
             ptrTag4(theiraddr),
             noc0.x,noc0.y);
    return b32.buf;
  }

  const char * formatT6RecvBuf32(Buf32 & b32, void * ouraddr, u32 pktsiz, u8 nonce, Dir8 fromd8) {
    snprintf(b32.buf,b32.SIZE,
             "%04x+%u#%02x%s%s",
             ptrTag4(ouraddr),
             pktsiz,
             (u32) nonce,
             "<-",
             dir8ToByteString(fromd8));
    return b32.buf;
  }

  const char * formatIHBlockBuf32(Buf32 & b32, InterHubBlock & ihb,Dir8 d8, u32 caridx) {
    InterHubPayload & pay = ihb.payload();
    u32 caratag = ptrTag4(&ihb);
    const char * d8s = dir8ToByteString(d8); 
    if (!pay.isValid()) 
      snprintf(b32.buf,b32.SIZE,"%04x/%sIB%u invalid",
               caratag,d8s,caridx);
    else
      snprintf(b32.buf,b32.SIZE,
               "%04x/%sIB%u%s~%x%s+%u",
               caratag,
               d8s,
               caridx,
               getCarStateName(ihb.getTCState()),
               ihb.getHeader().getPacketNonce() & 0xf,
               getNameFromIHPType(pay.getType()),
               ihb.currentTCSize());
    return b32.buf;
  }
}
