#pragma once        /* -*- C++ -*- */

#include "itype.h"
#include "utils.h"
#include "TTKMDStuff.h"
#include "OurTLBs.h"
#include "HostBlock.h"
#include "T6Image.h"

#include "T6Grid.h" // HACK TO ACCESS HUB T6Grids

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

    u8 * extractRISCVCodeFromTLBI(u32 baseaddress, u32 rvsize, u8 toTLBI) ;
    void dumpT6Image(const T6Image & image, u8 fromTLBI) ;

    void releaseTheHounds() ;

    void assertGoodMagic() ;
    s32 awaitResults() ;

    s32 scanHubGrids() ;
    s32 slowScanHostBlocks() ;

    typedef std::function< void(BHTag t6, HostBlock & hb, u8 oldfail, u8 newfail) > T6FailCallback;
    u32 newFails(T6FailCallback cb) ;

    void addHub(u32 tlbi,T6Grid & g) {
      MFM_API_ASSERT(!definedHubTLBI(tlbi),DUPLICATE_ENTRY);
      mHubTLBIToT6Grid.try_emplace(tlbi);
      mHubTLBIToT6Grid[tlbi] = HubValue(0,&g);
    }
  private:
    typedef u32 HubTLBI;
    typedef u32 ChangeCount;
    typedef std::pair<ChangeCount,T6Grid*> HubValue;
    typedef std::unordered_map<HubTLBI,HubValue> HubTLBIToT6Grid;
    HubTLBIToT6Grid mHubTLBIToT6Grid;
    bool definedHubTLBI(u32 tlbi) {
      return mHubTLBIToT6Grid.find(tlbi) != mHubTLBIToT6Grid.end();
    }

    u32 mCardNum;
    OurTLBs & mOurTLBs;
    u32 mLastTLBISlowScanned;
    u16 mStartDecayType;
  };
}


