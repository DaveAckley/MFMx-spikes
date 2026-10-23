#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"
#include "Fail.h"
#include "dev_mem_map.h" // for MEM_L1_SIZE
#include "FileIDs.h" // for GET_FILE_ID
#include "ExtraConstants.h" // for spOffHB etc

#define HOST_FATAL(...) do { } while (0)

#define MFM_API_ASSERT(expr,code) do { if (__builtin_expect(!(expr), 0)) FAIL(code); } while (0)

#define MFM_API_ASSERT_NULL(expr) MFM_API_ASSERT((expr)==0,NON_NULL_POINTER)
#define MFM_API_ASSERT_ZERO(expr) MFM_API_ASSERT((expr)==0,NON_ZERO)
#define MFM_API_ASSERT_NONZERO(expr) MFM_API_ASSERT((expr)!=0,ZERO)
#define MFM_API_ASSERT_ARG(expr) MFM_API_ASSERT(expr,ILLEGAL_ARGUMENT)
#define MFM_API_ASSERT_STATE(expr) MFM_API_ASSERT(expr,ILLEGAL_STATE)

////EXTRA T6 ONLY ASSERTS:
#define MFM_API_ASSERT_L1_ADDRESS(expr) MFM_API_ASSERT(((u32)(expr)) < MEM_L1_SIZE,OUT_OF_BOUNDS)

#define FATAL(code) \
  FATAL_AT(code,__FILE__,__LINE__)

#define MARK_JOIN2(a,b) a##b
#define MARK_JOIN(a,b)  MARK_JOIN2(a,b)

extern "C" { extern char __hostblock_start[]; }

#define BHANG()                                                   \
  do {                                                            \
    struct {                                                      \
      static void __attribute__ ((noinline)) x(void) {            \
        __asm__ __volatile__("nop" ::: "memory");                 \
      }                                                           \
    } keepBHangAwayFromPriorCodeSpeculation;                      \
    struct MARK_JOIN(bhang_, __COUNTER__) {                       \
      static void __attribute__ ((noinline)) x(void) {            \
        __asm__  __volatile__                                     \
          ("sw sp, %[spo](%[base])  # stash regs\n\t"             \
           "sw ra, %[rao](%[base])  # for host-side\n\t"          \
           "sw fp, %[fpo](%[base])  # stack dumps\n"              \
           "1:\t j 1b               # then hang\n\t"              \
           :                                                      \
           : [base] "r"((void*)__hostblock_start),                \
             [spo] "i"(spOffHB),                                  \
             [rao] "i"(raOffHB),                                  \
             [fpo] "i"(fpOffHB)                                   \
           : "memory");                                           \
      }                                                           \
    } m;                                                          \
    keepBHangAwayFromPriorCodeSpeculation.x();                    \
    m.x();                                                        \
  } while (0)


#define FATAL_AT(code,file,line) do {           \
    PrepareToDie(code,GET_FILE_ID(file),line);  \
    BHANG();                                    \
    __builtin_unreachable();                    \
  } while (0)

extern "C" void PrepareToDie(signed code,unsigned fileId,unsigned line) ;

extern "C" void t6hangprep(int code) ;

extern "C" uint32_t estimateStackUsage() ;

