#pragma once        /* -*- C++ -*- */

#include "itype.h"
#include "utils.h"
#include "TTKMDStuff.h"
#include "OurTLBs.h"
#include "HostBlock.h"
#include "T6Image.h"

namespace MFM {
  class CodeManager {
  public:
    CodeManager(u32 cardNum, OurTLBs & tlbs)
      : mCardNum(cardNum)
      , mOurTLBs(tlbs)
      , mLastTLBISlowScanned(U32_MAX)
      , mStartDecayType(U16_MAX)
    {
    }
    void setStartDecayType(u16 val) { mStartDecayType = val; }
    s32 deployRISCVCodeFromImage(const T6Image & image, u8 toTLBI) ;

    void releaseTheHounds() ;

    void assertGoodMagic() ;
    s32 awaitResults() ;

    s32 scanHubGrids() ;
    s32 slowScanHostBlocks() ;

    typedef std::function< void(BHTag t6, HostBlock & hb, u8 oldfail, u8 newfail) > T6FailCallback;
    u32 newFails(T6FailCallback cb) ;

  private:
    u32 mCardNum;
    OurTLBs & mOurTLBs;
    u32 mLastTLBISlowScanned;
    u16 mStartDecayType;
  };
}


