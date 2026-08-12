#pragma once        /* -*- C++ -*- */

#include "DefinedConstants.h" // for T6_IMAGE_BLOCK_ADDR
#include "itype.h"
#include "utils.h"
#include "TTKMDStuff.h"
#include "OurTLBs.h"
#include "HostBlock.h"
#include "T6Image.h"
#include "BGRImage.h"

#include "T6Grid.h" // HACK TO ACCESS HUB T6Grids
#include "QuietBox.h"
#include "ZHostDecompressor.h"

namespace MFM {
  class CodeManager {
  public:
    CodeManager(u32 chipNum, OurTLBs & tlbs)
      : mChipNum(chipNum)
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

    s32 scanHubGrid() ;
    BGRImageHD & clearT6GridImage() ;
    BGRImageHD & renderT6GridToImage(const T6Grid& t6g, const T6GridInfo & t6i) ;

    u32 readT6GridTotalChanges(T6Grid& t6g, u32 tlbi) ;
    void readAndDisplayT6Grid(T6Grid& t6g, u32 tlbi) ;

    std::string getFIDLIfAny(const char * imageName, u32 codeByteAddr, std::string * optfuncptr) ;

    s32 slowScanHostBlocks() ;

    typedef std::function< void(BHTag t6, HostBlock & hb, u8 oldfail, u8 newfail) > T6FailCallback;
    u32 newFails(T6FailCallback cb) ;

    typedef u32 HubTLBI;
    typedef u32 ChangeCount;
    struct HubValue {
      HubValue()
        : mChangeCount(0)
        , mTLBI(U32_MAX)
        , mChipNum(0)
      { }

      void init(HubTLBI tlbi, u32 chipnum) {
        mTLBI = tlbi;
        mZHD.init(mTLBI,chipnum);
      }
      ChangeCount mChangeCount;
      HubTLBI mTLBI;
      u32 mChipNum;
      ZHostDecompressor mZHD;
    };

    void addHub(u32 tlbi,u32 chipNum) {
      MFM_API_ASSERT(!definedHubTLBI(tlbi),DUPLICATE_ENTRY);
      mHubTLBIToHubValue.try_emplace(tlbi);
      mHubTLBIToHubValue[tlbi].init(tlbi,chipNum);
    }

    HubValue& getHubValue(u32 tlbi) {
      MFM_API_ASSERT(definedHubTLBI(tlbi),ILLEGAL_ARGUMENT);
      return mHubTLBIToHubValue[tlbi];
    }

  private:
    typedef std::unordered_map<HubTLBI,HubValue> HubTLBIToHubValue;
    HubTLBIToHubValue mHubTLBIToHubValue;
    bool definedHubTLBI(u32 tlbi) {
      return mHubTLBIToHubValue.find(tlbi) != mHubTLBIToHubValue.end();
    }

    u32 mChipNum;
    OurTLBs & mOurTLBs;
    u32 mLastTLBISlowScanned;
    u16 mStartDecayType;
  };
}


