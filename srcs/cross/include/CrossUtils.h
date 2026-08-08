#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "dev_mem_map.h"
#include "UxC.h" // for U8C
#include "S8C.h" 
#include "S16C.h"
#include "P4Atom.h"

namespace MFM {
  void markLogBlock(u16 fileid, u16 lineno,const P4Atom atom) ;
  void markLogBlock(u16 fileid, u16 lineno,const char * msg) ;
  void markLogBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag) ;
  void markLogBlock(u16 fileid, u16 lineno,const char * msg, const char * tag) ;
  void markLogBlock(u16 fileid, u16 lineno,const int val, const char * tag) ;
  void markLogBlock(u16 fileid, u16 lineno,const U8C c, const char * tag) ;
  void markLogBlock(u16 fileid, u16 lineno,const S8C c, const char * tag) ;
  void markLogBlock(u16 fileid, u16 lineno,const S16C c, const char * tag) ;
  void markLogBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag) ;

  void markHostBlock(u16 fileid, u16 lineno,const void * ptr, const char * tag) ;
  void markHostBlock(u16 fileid, u16 lineno,const char * msg, const char * tag) ;
  void markHostBlock(u16 fileid, u16 lineno,const int val, const char * tag) ;
  void markHostBlock64(u16 fileid, u16 lineno,const u64 val, const char * tag) ;
  void markHostBlock(u16 fileid, u16 lineno,const U8C c, const char * tag) ;
  void markHostBlock(u16 fileid, u16 lineno,const S8C c, const char * tag) ;
  void markHostBlock(u16 fileid, u16 lineno,const S16C c, const char * tag) ;

  inline void memoryFence() {
    asm volatile (
  "fence\n\t"   // on blackhole, flushes the (non-coherent) L0 data cache
  : // no outputs
  : // no inputs
  : "memory" // but clobbers memory
   );
  }

  /* Get select registers */

  inline u32 getRegisterSP(void) {
    register u32 val asm("x2");
    return val;
  }

  inline u32 getRegisterRA(void) {
    register u32 val asm("x1");
    return val;
  }

  inline u32 getRegisterFP(void) {
    register u32 val asm("x8");
    return val;
  }
  
  /** AI-DERIVED CODE SOSUMI
   * @brief Writes a value to an address, reads it back, and ensures the read-back
   *        value is consumed before returning.
   *        This version relies on the "read-after-write, then consume" pattern
   *        for memory ordering, as specified by Tenstorrent Blackhole documentation.
   *
   * @param address The memory address to write to and read from.
   * @param newvalue The value to write to the address.
   */
  inline void writeCommit32L1(u32* address, u32 newvalue) {
    u32 read_back_value; // Output for the read-back value (even though never used)

    asm volatile (
  "sw %1, 0(%2)\n\t"        // stash word (start store)
  "lw %0, 0(%2)\n\t"        // reread it (start load)
  "addi x0, %0, 0\n\t"      // consume value

  : "=r" (read_back_value)             // output to read_back_value (ensuring no optimization?)
  : "r" (newvalue), "r" (address)      // input: args %1 newvalue, %2 address

    // Clobbered registers/memory:
    // "memory": Tells GCC that the assembly code modifies memory,
    //           preventing it from reordering memory operations around this block.
    //           This is crucial for ensuring the write-read sequence is not
    //           reordered by the compiler with other C/C++ memory accesses.
  : "memory"
  );
  }


  /**
   * @brief same as writeCommit32L1 except for a 16 bit value
   *
   * @param address A pointer to the memory location to write to and read from (u16*).
   * @param newvalue The 16-bit value to write to the address.
   */
  inline void writeCommit16L1(u16* address, u16 newvalue) {
    u32 read_back_value;

    asm volatile (
  "sh %1, 0(%2)\n\t"
  "lhu %0, 0(%2)\n\t"
  "addi x0, %0, 0\n\t"
  : "=r" (read_back_value)
  : "r" (newvalue), "r" (address)
  : "memory"
   );
  }

  /**
   * @brief same as writeCommit32L1 except for an 8 bit value
   *
   * @param address A pointer to the memory location to write to and read from (u8*).
   * @param newvalue The 8-bit value to write to the address.
   */
  inline void writeCommit8L1(u8* address, u8 newvalue) { // newvalue is u8, address is u8*
    u32 read_back_value; // Still u32 because registers are 32-bit

    asm volatile (
  "sb %1, 0(%2)\n\t"
  "lbu %0, 0(%2)\n\t"
  "addi x0, %0, 0\n\t"
  : "=r" (read_back_value) // output reg
  : "r" (newvalue), "r" (address) // input regs
  : "memory" // memory clobber 
  );
  }
  
  template <class T>
  void writeCommitL1(T* address, T newvalue) { 
    if constexpr (sizeof(T) == 4) {
      writeCommit32L1(reinterpret_cast<u32*>(address),static_cast<u32>(newvalue));
    } else if constexpr (sizeof(T) == 2) {
      writeCommit16L1(reinterpret_cast<u16*>(address),static_cast<u16>(newvalue));
    } else if constexpr (sizeof(T) == 1) {
      writeCommit8L1(reinterpret_cast<u8*>(address),static_cast<u8>(newvalue));
    } else {
      static_assert(sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1,
                    "Unsupported type size for writeCommitL1");
    }
  }
}


#define C9printf(...) CNprintf(9, __VA_ARGS__)

#define CNprintf(COUNTCN, ...)                                          \
  do {                                                                  \
  static u32 __var;                                                     \
  if (__var++ < COUNTCN)                                                \
    CUprintf(__var, __FILE__,__LINE__, __VA_ARGS__);                    \
  if (__var == COUNTCN)                                                 \
    CUprintf(__var, __FILE__,__LINE__, "MUTING...\n");                  \
  } while (0)

/// SEE NOTE BELOW ABOUT XXX_MAYBE_DIE_FUNC
#define DIEWAY() /*DIEWAYDOIT()*/
#define DIEWAYDOIT() do {                       \
  static u32 count;                             \
  XXX_MAYBE_DIE_FUNC(__FILE__,__LINE__,count);  \
  } while (0)

#define SHOWADDR(var) /*SHOWADDRDOIT(var)*/
#define SHOWADDRDOIT(var)do {                           \
  _Pragma("GCC diagnostic push")                        \
  _Pragma("GCC diagnostic ignored \"-Wstrict-aliasing\"")     \
  DP.printf(                                    \
  "%s:%d:(%d,%d,%s)SHD:%s@0x%x=0x%x/%d\n",      \
    stripDirs(__FILE__),__LINE__,               \
    fAll.mNoC0.x, fAll.mNoC0.y,                 \
    hartName(fAll.mHartNum),                    \
    #var, (u32) &(var),                         \
    *(u32*)&(var), *(u32*)&(var));              \
  _Pragma("GCC diagnostic pop")                 \
  } while (0)                                 

#define XXX_DEBUG_FUNC(...) /* XXX_DEBUG_FUNC_DOIT(__FILE__,__LINE__) */

namespace MFM {
  inline bool isInL1(uptr addr) { return /*addr >= MEM_L1_BASE &&*/ addr < MEM_L1_SIZE; }
  inline bool isInL1(u32 * addr) { return isInL1((uptr) addr); }

  void memset_s(void* addr, u8 byte, u32 count) ;

  int strcmp_s(const char *s1, const char *s2) ;

  void CUprintf(u32 count, const char * file, u32 line, const char * fmt, ...);

  void XXX_DEBUG_FUNC_DOIT(const char * file, u32 line) ;

  /*** NOTE: XXX_MAYBE_DIE_FUNC IS HARDCODED TO ONE IMAGIC CONSTANT
       IT IS NOT NOT NOT NOT FOR GENERAL USE
   ***/
  void XXX_MAYBE_DIE_FUNC(const char * file, u32 line, u32& count);

  const char * stripDirs(const char * path, u32 dircount = 2u);

  static constexpr u32 ACBUF_SIZE = 40;
  using AtomCharBuf = char [ACBUF_SIZE];
  bool formatP4Atom(const P4Atom a, AtomCharBuf buf) ;

  char * formatCountedByte(u32 count, u8 byte) ;
}
