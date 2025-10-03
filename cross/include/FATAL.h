#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include "utils.h"

#include "Fail.h"
#include "FastLocal.h"

#define MFM_API_ASSERT(expr,code) do { if (__builtin_expect(!(expr), 0)) FAIL(code); } while (0)

#define MFM_API_ASSERT_ON_HART(expr) MFM_API_ASSERT(fAll.mHartNum==(expr),WRONG_HART)

#define MFM_API_ASSERT_NULL(expr) MFM_API_ASSERT((expr)==0,NON_NULL_POINTER)
#define MFM_API_ASSERT_ZERO(expr) MFM_API_ASSERT((expr)==0,NON_ZERO)
#define MFM_API_ASSERT_NONZERO(expr) MFM_API_ASSERT((expr)!=0,ZERO)
#define MFM_API_ASSERT_ARG(expr) MFM_API_ASSERT(expr,ILLEGAL_ARGUMENT)
#define MFM_API_ASSERT_STATE(expr) MFM_API_ASSERT(expr,ILLEGAL_STATE)

#define FATAL(code) \
  FATAL_AT(code,__FILE__,__LINE__)

#define FATAL_AT(code,file,line) \
  DieHereNow(code,file,line)

extern "C" void DieHereNow(signed code,const char * file,unsigned line) __attribute__((__noreturn__)) ;

extern "C" void t6hang(int code) __attribute__ ((__noreturn__)) ;

