#include "itype.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "Printf.h" // for t6InitPrinters()
#include "Debug.h" 
#include "DefaultLives.h" 
#include "HartTasks.h" // XXX


// Baby RV Service APIs 
#include "FastLocal.h"
#include "FastB.h"   // for hartMainB
#include "FastT0.h"  // for hartMainT0
#include "FastT1.h"  // for hartMainT1
#include "FastT2.h"  // for hartMainT2
#include "FastNC.h"  // for hartMainNC

namespace MFM {
  HostBlock theHostBlock __attribute__ ((section(".hostblock")));
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

  HartTaskerPublicState theHartTaskerPublicState;

  int t6inithostblock(HostBlock &hb) { // RUNS ON HARTB ONLY
    u32 node_id = *NOC_NODE_ID0;
    hb.mNoC0.x = ((node_id >> 0) & 0x3f);
    hb.mNoC0.y = ((node_id >> 6) & 0x3f);
    hb.mTLBI = U8C::makeTLBIFromNoCCoord({hb.mNoC0.x,hb.mNoC0.y});
    RCFlag flags = RCFlag::RC_ZERO;
    hb.addBytes('t',flags?'6':'X');
    return 0;
  }
  int t6otherinits(HostBlock &hb) { // RUNS ON HARTB ONLY
    hb.addBytes('o','i');
    t6InitPrinters(hb);
    theHartTaskerPublicState.init();
    return 0;
  }

  static void ouriba0OK(u16 f, u16 l) {
    //    constexpr u32 SLOTS = 100;
    // constexpr u32 SLOTS = 3;
    //    constexpr u32 SLOTS = 1;
    //    constexpr u32 SLOTS = 7;
    //constexpr u32 SLOTS = 6;
    //    constexpr u32 SLOTS = 5;
    //    constexpr u32 SLOTS = 4;
    //    constexpr u32 SLOTS = 3;
    constexpr u32 SLOTS = 2;
    static u32 shadow[SLOTS];
    static bool first;

    //    const u32 *ibux14 = (u32*) 0x14;  // '= &theImageBlock;'
    //    const u32 *ibux14 = (u32*) 0x18;  // '= &theImageBlock+1;'
    constexpr u32 OFFSET = 1;
    const u32 *ibux14 = ((u32*) 0x14) + OFFSET;  // '= &theImageBlock+2;'

    if (!first) {
      for (u32 i = 0; i < SLOTS; ++i)
        shadow[i] = ibux14[i];
      first = true;
    } else {
      bool hit = false;
      for (u32 i = 0; i < SLOTS; ++i) {
        if (shadow[i] != ibux14[i]) {
          FIDLPTAG(f,l,i=,&ibux14[i]);
          FIDLXTAG(f,l,was,shadow[i]);
          FIDLXTAG(f,l,now,ibux14[i]);
          hit = true;
        }
      }
      HBASSERT_EQ(hit,false);
    }
  }

  int t6main(HostBlock& hb) {
    //HBPTAG(t6main,hartName(fAll.mHartNum));
    //setGlobalDebugHook(ouriba0OK);
    //HBPTAG(HOOKT,hartName(fAll.mHartNum));

    HartTaskerPrivate udaMan;
    udaMan.init(theHartTaskerPublicState);
    udaMan.run();
    HBNOTE(<<ENDINSQ>>);

    //    HBPTAG(sHOOKT,(void*) theGlobalDebugHook);
    LOGMARK;

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
  int t6inithostblock(MFM::HostBlock *hb) { return MFM::t6inithostblock(*hb); }
  int t6otherinits(MFM::HostBlock *hb) { return MFM::t6otherinits(*hb); }
  int t6main(MFM::HostBlock* hb) { return MFM::t6main(*hb); }
}
