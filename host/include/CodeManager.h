#pragma once        /* -*- C++ -*- */

#include "itype.h"
#include "utils.h"
#include "TTKMDStuff.h"
#include "OurTLBs.h"

namespace MFM {
  class CodeManager {
  public:
    CodeManager(OurTLBs & tlbs)
      : mOurTLBs(tlbs)
    {
    }
    s32 deployRISCVCodeFromFile(const char * path) ;
    s32 deployThisRISCVCode(const char * rvcode, u32 rvsize) ;

    void assertGoodMagic() ;
    s32 awaitResults() ;

  private:
    OurTLBs & mOurTLBs;
    u32 mRVCodeSize;
  };
}


