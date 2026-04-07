#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"
#include "Fail.h"
#include "dev_mem_map.h" // for MEM_L1_SIZE
#include "FileIDs.h" // for GET_FILE_ID

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

#define FATAL_AT(code,file,line) \
  DieHereNow(code,GET_FILE_ID(file),line)

extern "C" void DieHereNow(signed code,unsigned fileId,unsigned line) __attribute__((__noreturn__)) ;

extern "C" void t6hang(int code) __attribute__ ((__noreturn__)) ;

extern "C" uint32_t estimateStackUsage() ;

