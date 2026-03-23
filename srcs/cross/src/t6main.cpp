#include "itype.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "Printf.h" // for t6InitPrinters()

// Baby RV Service APIs 
#include "FastLocal.h"
#include "FastB.h"   // for hartMainB
#include "FastT0.h"  // for hartMainT0
#include "FastT1.h"  // for hartMainT1
#include "FastT2.h"  // for hartMainT2
#include "FastNC.h"  // for hartMainNC

namespace MFM {
  HostBlock theHostBlock __attribute__ ((section(".hostblock"))) = {
    .mHBMagic = HostBlock::HBMAGIC,
    .mPos = { U8_MAX, U8_MAX-1u },  // bad init to overwrite
    .mTLBI = U8_MAX-2u,             //  "
    .mHBCigam = HostBlock::HBCIGAM
  };
#if 0
  //  ImageBlock theImageBlock __attribute__ ((section(".imageblock"))) = {
#define DECLARE_THE_IMAGE_BLOCK(IMAGECODE,ENTRIES)                      \
  ImageBlockT<ENTRIES> theImageBlock __attribute__ ((section(".imageblock"))) = { {  \
      .mIBMagic = ImageBlockHeader::IBMAGIC,                            \
      .mImageCode = IMAGECODE,                       \
      .mEntries = ENTRIES                            \
      .mIBCheck = (IMAGECODE)^(ENTRIES<<2u)          \
    } }
    
  DECLARE_THE_IMAGE_BLOCK(0xea,18);
#endif

  int t6setup(HostBlock &hb) { // RUNS ON HARTB ONLY
    u32 node_id = *NOC_NODE_ID0;
    //    hb.mPos.x = 8;
    //    hb.mPos[y = 7;
    hb.mPos.x = ((node_id >> 0) & 0x3f);
    hb.mPos.y = ((node_id >> 6) & 0x3f);
    /*
    if (theImageBlock.mImageCode[0]=='I') {
      theImageBlock.mImageMajVer = (u8) (u32) &theImageBlock;
      hb.mPos.x = 0x3f;
      }*/
    hb.mTLBI = U8C::makeTLBIFromNoC0Coord({hb.mPos.x,hb.mPos.y});
    //    XXX_DEBUG_FUNC(__FILE__,__LINE__);
    t6InitPrinters(hb,theT6ElevatorTransport);
    //XXX_DEBUG_FUNC(__FILE__,__LINE__);
    return 0;
  }

  int t6main(HostBlock& hb) {
    switch (fAll.mHartNum) {
    case 0u: return hartMainB(hb);
    case 1u: return hartMainT0(hb);
    case 2u: return hartMainT1(hb);
    case 3u: return hartMainT2(hb);
    case 4u: return hartMainNC(hb);
    }
    return 0; // NOT REACHED
  }
}

extern "C" {
  int t6setup(MFM::HostBlock *hb) { return MFM::t6setup(*hb); }
  int t6main(MFM::HostBlock* hb) { return MFM::t6main(*hb); }
}
