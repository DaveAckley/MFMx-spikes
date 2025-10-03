#pragma once    /* -*- C++ -*- */

//#include <stdexcept>
#include "utils.h"

/// FATAL #defined separately in {host|cross}/include/FATAL.h 
#include "FATAL.h"

#define XX(name) name,
enum FAILCode {                 // Declare failcodes outside MFM:: grr
  DUMMY = -100,
#include "FailCodes.h"
};
#undef XX

#define FAIL(code) FATAL(FAILCode::code)

#define MFM_API_ASSERT(expr,code) do { if (__builtin_expect(!(expr), 0)) FAIL(code); } while (0)
#define MFM_API_ASSERT_NONNULL(expr) MFM_API_ASSERT(expr,NULL_POINTER)
#define MFM_API_ASSERT_NULL(expr) MFM_API_ASSERT((expr)==0,NON_NULL_POINTER)
#define MFM_API_ASSERT_ZERO(expr) MFM_API_ASSERT((expr)==0,NON_ZERO)
#define MFM_API_ASSERT_NONZERO(expr) MFM_API_ASSERT((expr)!=0,ZERO)
#define MFM_API_ASSERT_ARG(expr) MFM_API_ASSERT(expr,ILLEGAL_ARGUMENT)
#define MFM_API_ASSERT_STATE(expr) MFM_API_ASSERT(expr,ILLEGAL_STATE)
