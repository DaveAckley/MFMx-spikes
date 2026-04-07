#pragma once /* -*- C++ -*- */

#ifndef BUILD_HOST

#include "CrossUtils.h"
#include "FastLocal.h"          // for fAll
#include "ImageBlock.h"         // for ImageBlockHeader

#define SNAP(COUNT,CODE)                        \
  do {                                          \
  static u32 __count = 0u;                      \
  if (__count++ < COUNT) {                      \
    HBPVAL(__count); CODE;                      \
  }                                             \
  } while (0)

#define HBMARK do {                                                     \
     markHostBlock(GET_FILE_ID(__FILE__),__LINE__,                      \
                   getNameFromImageCode((ImageCode) (((ImageBlockHeader*)0x14)->getImageCode()))); \
    } while (0)
#define HBNOTE(MSG) do { markHostBlock(GET_FILE_ID(__FILE__),__LINE__,MSG); } while(0)
#define HBPVAL(PRTABLEVAL) do { markHostBlock(GET_FILE_ID(__FILE__),__LINE__,PRTABLEVAL); } while(0)
#define HBXVAL(PRTABLEVAL) do { markHostBlock(GET_FILE_ID(__FILE__),__LINE__,(void*)(PRTABLEVAL)); } while(0)

#define HBASSERT_COMP(A,B,OP) do {              \
  if (!(A OP B)) {                              \
    HBNOTE("HBANG!");                           \
    HBPVAL(A);                                  \
    HBPVAL(B);                                  \
  }                                             \
  MFM_API_ASSERT(A OP B,DESCRIBED_FAILURE);     \
  } while(0);                                   \
  
#else

#define SNAP(COUNT,CODE) do { } while (0)
#define HBMARK do { } while (0)
#define HBNOTE(MSG) do { } while (0)
#define HBPVAL(INTISHVAL) do { } while (0)
#define HBXVAL(INTISHVAL) do { } while (0)
#define HBASSERT_COMP(A,B,OP) do { } while (0)

#endif

#define HBASSERT_EQ(A,B) HBASSERT_COMP(A,B,==)
#define HBASSERT_NE(A,B) HBASSERT_COMP(A,B,!=)
#define HBASSERT_LS(A,B) HBASSERT_COMP(A,B,<)
#define HBASSERT_LSEQ(A,B) HBASSERT_COMP(A,B,<=)

