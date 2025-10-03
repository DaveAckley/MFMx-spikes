#pragma once  /* -*- C++ -*- */

#include <cstdio>
#include "itype.h"
#include "Fail.h"

#define ASSERT(expr)                            \
  MFM_API_ASSERT(expr,ASSERTION_FAILED)

#define HOST_FATAL(code, ...)                   \
  do {                                          \
    fprintf(stderr, __VA_ARGS__);               \
    FATAL(code);                                \
  } while (0)

#define FATAL(code) \
  DieHereNow(code,__FILE__,__LINE__,#code)

extern "C" void DieHereNow(signed code,const char * file,unsigned line,const char * scode) __attribute__((__noreturn__)) ;


