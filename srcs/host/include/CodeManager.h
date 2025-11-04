#pragma once        /* -*- C++ -*- */

#include "itype.h"
#include "utils.h"
#include "TTKMDStuff.h"
#include "OurTLBs.h"
#include "HostBlock.h"

namespace MFM {
  class CodeManager {
  public:
    CodeManager(u32 cardNum, OurTLBs & tlbs)
      : mCardNum(cardNum)
      , mOurTLBs(tlbs)
      , mRVCodeSize(0u)
      , mLastTLBISlowScanned(U32_MAX)
      , mStartDecayType(U16_MAX)
    {
    }
    void setStartDecayType(u16 val) { mStartDecayType = val; }
    s32 deployRISCVCodeFromFile(const char * path) ;
    s32 deployThisRISCVCode(const char * rvcode, u32 rvsize) ;

    void releaseTheHounds() ;

    void assertGoodMagic() ;
    s32 awaitResults() ;

    s32 slowScanHostBlocks() ;

    typedef std::function< void(BHTag t6, HostBlock & hb, u8 oldfail, u8 newfail) > T6FailCallback;
    u32 newFails(T6FailCallback cb) ;

  private:
    u32 mCardNum;
    OurTLBs & mOurTLBs;
    u32 mRVCodeSize;
    u32 mLastTLBISlowScanned;
    u16 mStartDecayType;
  };
}


