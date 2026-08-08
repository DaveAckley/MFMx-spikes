#pragma once /* -*- C++ -*- */

#ifndef BUILD_HOST

#include "CrossUtils.h"
#include "FastLocal.h"          // for fAll
#include "ImageCode.h"

namespace MFM {
  typedef void (*DebugHookFncPtr)(u16 filed, u16 lineno);
  extern DebugHookFncPtr theGlobalDebugHook;
  inline void setGlobalDebugHook(DebugHookFncPtr p) { theGlobalDebugHook = p; }
}
#define HOOKIT() \
  do { if (theGlobalDebugHook) (*theGlobalDebugHook)(GET_FILE_ID(__FILE__),__LINE__); } while (0)
  

#define EACH(COUNT,CODE)                          \
  do {                                            \
    static u32 __EACHNUM__ = 0u;                  \
    if ((__EACHNUM__++ % COUNT) == 0) {           \
      CODE;                                       \
    }                                             \
  } while (0)

#define SNAP(COUNT,CODE)                        \
  do {                                          \
  static u32 __count = 0u;                      \
  if (__count++ < COUNT) {                      \
    CODE;                                       \
  }                                             \
  if (__count == COUNT) HBPTAG(SNAPT,__count);  \
  } while (0)

#define ONE_PING_ONLY()                         \
  do {                                          \
    static u32 __pings;                         \
    HBPTAG(1PO,__FUNCTION__);                   \
    ++__pings;                                  \
    memoryFence();                              \
    HBASSERT_LE(__pings,1);                     \
  } while(0)

#define LOGPX(EXPR) LOGPTAG(EXPR,EXPR)
#define LOGXX(EXPR) LOGXTAG(EXPR,EXPR)
#define LOGMARK FIDLMARKLG(GET_FILE_ID(__FILE__),__LINE__)
#define FIDLMARKLG(F,L) do {                                            \
    markLogBlock(F,L, getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode)); \
  } while (0)

#define LOGNOTE(MSG) LOGPTAG(MSG,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode))

#define LOGATOM(ATOM) FIDLMARKLGATOM(GET_FILE_ID(__FILE__),__LINE__,ATOM)
#define FIDLMARKLGATOM(F,L,ATOM) do {                                   \
    markLogBlock(F,L,ATOM);                                             \
  } while (0)

#define LOGMSG(MSG) FIDLMARKLGMSG(GET_FILE_ID(__FILE__),__LINE__,MSG)
#define FIDLMARKLGMSG(F,L,MSG) do {                                     \
    markLogBlock(F,L,MSG);                                              \
  } while (0)

#define LOGPVAL(PRTABLEVAL) FIDLPVALLG(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLPVALLG(F,L,P) do { markLogBlock(F,L,P,0); } while(0)

#define LOGPTAG(TAG,PRTABLEVAL) FIDLPTAGLG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLPTAGLG(F,L,T,P) do { markLogBlock(F,L,P,"" #T ":"); } while(0)

#define LOGPTAG64(TAG,PRTABLEVAL) FIDLPTAGLG64(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLPTAGLG64(F,L,T,P) do { markLogBlock64(F,L,P,"" #T ":"); } while(0)

#define LOGXTAG(TAG,PRTABLEVAL) FIDLXTAGLG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLXTAGLG(F,L,T,P) do { markLogBlock(F,L,(void*)(P),"" #T ":"); } while(0)

#define HBMARK FIDLMARK(GET_FILE_ID(__FILE__),__LINE__)
#define FIDLMARK(F,L) do {                                              \
    markHostBlock(F,L, getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode),0); \
  } while (0)
#define HBNOTE(MSG) HBPTAG(MSG,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode))
//FIDLNOTE(GET_FILE_ID(__FILE__),__LINE__,MSG)
//#define FIDLNOTE(F,L,M) do { markHostBlock(F,L,M,0); } while(0)

#define HBPVAL(PRTABLEVAL) FIDLPVAL(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLPVAL(F,L,P) do { markHostBlock(F,L,P,0); } while(0)

#define HBPVAL64(PRTABLEVAL) FIDLPVAL64(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLPVAL64(F,L,P) do { markHostBlock64(F,L,P,0); } while(0)

#define HBXVAL(PRTABLEVAL) FIDLXVAL(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL)
#define FIDLXVAL(F,L,P) do { markHostBlock(F,L,(void*)(P),0); } while(0)

#define HBPTAG(TAG,PRTABLEVAL) FIDLPTAG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLPTAG(F,L,T,P) do { markHostBlock(F,L,P," " #T ":"); } while(0)

#define HBPTAG64(TAG,PRTABLEVAL) FIDLPTAG64(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLPTAG64(F,L,T,P) do { markHostBlock64(F,L,P," " #T ":"); } while(0)

#define HBXTAG(TAG,PRTABLEVAL) FIDLXTAG(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLXTAG(F,L,T,P) do { markHostBlock(F,L,(void*)(P)," " #T ":"); } while(0)

#define HBXTAG64(TAG,PRTABLEVAL) FIDLXTAG64(GET_FILE_ID(__FILE__),__LINE__,TAG,PRTABLEVAL)
#define FIDLXTAG64(F,L,T,P) do { markHostBlock64(F,L,(P)," " #T ":"); } while(0)

#define HBPX(EXPR) HBPTAG(EXPR,EXPR)
#define HBXX(EXPR) HBXTAG(EXPR,EXPR)

#define HBASSERT_COMP(A,B,OP) do {              \
  if (!(A OP B)) {                              \
    HBPTAG(HBANG!,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode)); \
    HBPVAL(A);                                  \
    HBPVAL(B);                                  \
  }                                             \
  MFM_API_ASSERT(A OP B,DESCRIBED_FAILURE);     \
  } while(0)
  
#else //ifndef BUILD_HOST
#include "HostUtils.h"

#define EACH(COUNT,CODE)                          \
  do {                                            \
    thread_local u32 __EACHNUM__ = 0u;            \
    if ((__EACHNUM__++ % COUNT) == 0) {           \
      CODE;                                       \
    }                                             \
  } while (0)

#define SNAP(COUNT,CODE)                        \
  do {                                          \
    thread_local u32 __count = 0u;              \
  memoryFence();  /* _count is super racy */    \
  if (__count++ < COUNT) {                      \
    CODE;                                       \
  }                                             \
  if (__count == COUNT) HTprintf("SNAPT %ul\n",__count);  \
  } while (0)


#define HBMARK do { } while (0)
#define HBNOTE(MSG) do { } while (0)
#define HBPVAL(INTISHVAL) do { } while (0)
#define HBXVAL(INTISHVAL) do { } while (0)
#define HBPTAG(TAG,PRTABLEVAL) do { } while(0)
#define HBXTAG(TAG,PRTABLEVAL) do { } while (0)
#define HBASSERT_COMP(A,B,OP) do {              \
  MFM_API_ASSERT(A OP B,DESCRIBED_FAILURE);     \
 } while(0)
#define HBPX(EXPR) HBPTAG(EXPR,EXPR)
#define HBXX(EXPR) HBXTAG(EXPR,EXPR)

#define LOGPX(EXPR) do { } while (0)
#define LOGXX(EXPR) do { } while (0)
#define LOGMARK do { } while (0)
#define LOGNOTE(MSG) do { } while (0)
#define LOGATOM(MSG) do { } while (0)
#define LOGMSG(MSG) do { } while (0)
#define LOGPVAL(PRTABLEVAL) do { } while (0)
#define LOGPTAG(TAG,PRTABLEVAL) do { } while (0)
#define LOGXTAG(TAG,PRTABLEVAL) do { } while (0)

#endif

#define HBASSERT_EQ(A,B) HBASSERT_COMP(A,B,==)
#define HBASSERT_NE(A,B) HBASSERT_COMP(A,B,!=)
#define HBASSERT_LT(A,B) HBASSERT_COMP(A,B,<)
#define HBASSERT_GT(A,B) HBASSERT_COMP(A,B,>)
#define HBASSERT_GE(A,B) HBASSERT_COMP(A,B,>=)
#define HBASSERT_LE(A,B) HBASSERT_COMP(A,B,<=)

