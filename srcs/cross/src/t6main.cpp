#include "itype.h"
#include "HostBlock.h"
#include "ImageBlock.h"
#include "Printf.h" // for t6InitPrinters()
#include "Debug.h" 
#include "StandardLife.h" 
#include "DefinedConstants.h" // for T6_IMAGE_BLOCK_ADDR
#include "ExtraConstants.h" // for NOC_NODE_ID0

// Baby RV Service APIs 
#include "FastLocal.h"
#include "HartTasksLib.h"  // for RCFlag

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

  int t6inithostblock(HostBlock &hb) { // RUNS ON HARTB ONLY
    u32 node_id = *NOC_NODE_ID0;
    hb.mNoC0.x = ((node_id >> 0) & 0x3f);
    hb.mNoC0.y = ((node_id >> 6) & 0x3f);
    hb.mTLBI = U8C::makeTLBIFromNoCCoord({hb.mNoC0.x,hb.mNoC0.y});
    RCFlag flags = RCFlag::RC_ZERO;
    hb.addBytes('t',flags?'6':'X');
    return 0;
  }

  extern void setupHartTaskerInits(); // in allimg/src/HartTasks.cpp
  extern void runHartTaskerInits(); // in allimg/src/HartTasks.cpp

  int t6otherinits(HostBlock &hb) { // RUNS ON HARTB ONLY
    { // INIT MDist TABLES
      const MDist4 md;
      md.init();
      hb.addBytes('m','d');
    }

    t6InitPrinters(hb);
    setupHartTaskerInits();
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

    //    const u32 *ibux14 = (u32*) 0x18;  // '= &theImageBlock;'
    //    const u32 *ibux14 = (u32*) 0x1c;  // '= &theImageBlock+1;'
    constexpr u32 OFFSET = 1;
    const u32 *ibux14 = ((u32*) T6_IMAGE_BLOCK_ADDR) + OFFSET;  // '= &theImageBlock+2;'

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
    runHartTaskerInits();

    //    HBPTAG(sHOOKT,(void*) theGlobalDebugHook);
    LOGMARK;

    liveTheStandardLife(hb);
    return 0; // NOT REACHED
  }
}

extern "C" {
  int t6inithostblock(MFM::HostBlock *hb) { return MFM::t6inithostblock(*hb); }
  int t6otherinits(MFM::HostBlock *hb) { return MFM::t6otherinits(*hb); }
  int t6main(MFM::HostBlock* hb) { return MFM::t6main(*hb); }
}
