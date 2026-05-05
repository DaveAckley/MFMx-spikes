#pragma once /* -*- C++ -*- */

#ifndef BUILD_HOST

#include "CrossUtils.h"
#include "FastLocal.h"          // for fAll
#include "ImageBlock.h"         // for ImageBlockHeader

namespace MFM {
  typedef void (*DebugHookFncPtr)(u16 filed, u16 lineno);
  extern DebugHookFncPtr theGlobalDebugHook;
  inline void setGlobalDebugHook(DebugHookFncPtr p) { theGlobalDebugHook = p; }
}
#define HOOKIT() \
  do { if (theGlobalDebugHook) (*theGlobalDebugHook)(GET_FILE_ID(__FILE__),__LINE__); } while (0)
  

#define SNAP(COUNT,CODE)                        \
  do {                                          \
  static u32 __count = 0u;                      \
  if (__count++ < COUNT) {                      \
    CODE;                                       \
  }                                             \
  if (__count == COUNT) HBPTAG(SNAPT,__count);  \
  } while (0)

#define HBMARK FIDLMARK(GET_FILE_ID(__FILE__),__LINE__)
#define FIDLMARK(F,L) do {                                              \
    markHostBlock(F,L, getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode),0); \
  } while (0)
#define HBNOTE(MSG) HBPTAG(MSG,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode))
//FIDLNOTE(GET_FILE_ID(__FILE__),__LINE__,MSG)
//#define FIDLNOTE(F,L,M) do { markHostBlock(F,L,M,0); } while(0)

#define HBPVAL(PRTABLEVAL) FIDLPVAL(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLPVAL(F,L,P) do { markHostBlock(F,L,P,0); } while(0)

#define HBXVAL(PRTABLEVAL) FIDLXVAL(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLXVAL(F,L,P) do { markHostBlock(F,L,(void*)(P),0); } while(0)

#define HBPTAG(TAG,PRTABLEVAL) FIDLPTAG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLPTAG(F,L,T,P) do { markHostBlock(F,L,P," " #T ":"); } while(0)

#define HBXTAG(TAG,PRTABLEVAL) FIDLXTAG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLXTAG(F,L,T,P) do { markHostBlock(F,L,(void*)(P)," " #T ":"); } while(0)

#define HBASSERT_COMP(A,B,OP) do {              \
  if (!(A OP B)) {                              \
    HBPTAG(HBANG!,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode)); \
    HBPVAL(A);                                  \
    HBPVAL(B);                                  \
  }                                             \
  MFM_API_ASSERT(A OP B,DESCRIBED_FAILURE);     \
  } while(0)
  
#else

#define SNAP(COUNT,CODE) do { } while (0)
#define HBMARK do { } while (0)
#define HBNOTE(MSG) do { } while (0)
#define HBPVAL(INTISHVAL) do { } while (0)
#define HBXVAL(INTISHVAL) do { } while (0)
#define HBPTAG(TAG,PRTABLEVAL) do { } while(0)
#define HBXTAG(TAG,PRTABLEVAL) do { } while (0)
#define HBASSERT_COMP(A,B,OP) do {              \
  MFM_API_ASSERT(A OP B,DESCRIBED_FAILURE);     \
 } while(0)

#endif

#define HBASSERT_EQ(A,B) HBASSERT_COMP(A,B,==)
#define HBASSERT_NE(A,B) HBASSERT_COMP(A,B,!=)
#define HBASSERT_LS(A,B) HBASSERT_COMP(A,B,<)
#define HBASSERT_GT(A,B) HBASSERT_COMP(A,B,>)
#define HBASSERT_GTEQ(A,B) HBASSERT_COMP(A,B,>=)
#define HBASSERT_LSEQ(A,B) HBASSERT_COMP(A,B,<=)

