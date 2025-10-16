#include "itype.h"
#include "HostBlock.h"
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
    .mXPos = U8_MAX,            // bad init to overwrite
    .mYPos = U8_MAX-1u,         //  "
    .mTLBI = U8_MAX-2u,         //  "
    .mHBCigam = HostBlock::HBCIGAM
  };

  int t6setup(HostBlock &hb) {
    u32 node_id = *NOC_NODE_ID0;
    hb.mXPos = ((node_id >> 0) & 0x3f);
    hb.mYPos = ((node_id >> 6) & 0x3f);
    hb.mTLBI = U16C::makeTLBIFromNocCoord({hb.mXPos,hb.mYPos});
    t6InitPrinters(hb);
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
