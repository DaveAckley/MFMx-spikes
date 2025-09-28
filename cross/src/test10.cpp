#include "itype.h"
#include "RandMT.h"
#include "HostBlock.h"
#include "AtomicLock.h"
#include "ExtraConstants.h" // for NOC_NODE_ID0
#include "P2PElevator.h"
#include "T6ElevatorTransport.h"

#define STR1(A) #A
#define STR(A) STR1(A)
#define CONC1(A,B) A ## B
#define CONC(A,B) CONC1(A,B)

#define FAST_LOCAL(typedecl,name,hart)       \
  typedecl                                   \
  name                                       \
  __attribute__ ((section(STR(CONC(.fastram_,hart)))))  \

namespace MFM {
  HostBlock theHostBlock __attribute__ ((section(".hostblock"))) = {
    .mHBMagic = HostBlock::HBMAGIC,
    .mHBCigam = HostBlock::HBCIGAM
  };

  struct GB { u32 clams; };
  struct G0 { bool bong; };
  struct G1 { s32 snig; u32 snog; u32 snug; };
  struct G2 { RandMT mRandom; };
  struct GN { };

  FAST_LOCAL(GB,gb,b);
  FAST_LOCAL(G0,g0,t0);
  FAST_LOCAL(G1,g1,t1);
  FAST_LOCAL(G2,g2,t2);
  FAST_LOCAL(GN,gn,nc);

  /*union D {
    D() { }
    GB gb;
    G0 g0;
    G1 g1;
    G2 g2;
    GN gn;
  } d __attribute__ ((section(".fastram"), aligned(0x4)));
  */

  u32 wastoid;
  AtomicLock mylock;  // static -> can't hold shared locks in private RAM

  struct DemoCar {
    u8 mBytes[256];
  };
  typedef P2PElevatorPlatform<DemoCar,2> MyPlatform;
  MyPlatform myPlatform(true);

  int hartMainB(HostBlock & hb, GB & gb) {
    T6ElevatorTransport t6et(hb);
    u32 node_id = *NOC_NODE_ID0;
    u32 node_endpoint_id = *NOC_ENDPOINT_ID0;
    hb.mCommonArgs[0] = node_id;
    hb.mCommonArgs[1] = (u32) hb.mHostBaseAddrLo; //ET_NIU_BASE;
    hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;
    //hb.mCommonArgs[1] = node_endpoint_id;
    hb.mXPos = ((node_id >> 0) & 0x3f);
    hb.mYPos = ((node_id >> 6) & 0x3f);

    //hb.addString("hart B: lock test\n");
    mylock.acquireLock();
    hb.addString("hart B: i hold the test lock\n");
    if (!mylock.tryLock())
      hb.addString("hart B: and i can't take it again because i already have it doh\n");
    mylock.releaseLock();
    hb.addString("hart B: lock released\n");
    return 0;
  }
  int hartMainT0(HostBlock & hb, G0 & g0) {
    ++wastoid;
    return 0;
  }
  int hartMainT1(HostBlock & hb, G1 & g1) {
    if (wastoid > 3) --wastoid;
    return 0;
  }
  int hartMainT2(HostBlock & hb, G2 & g2) {
    g2.mRandom.seedMT_MFM(2);
    hb.addString("hart T2 report: Goodbye World\n");
    return g2.mRandom.randomMT() & 0xffffff;
  }
  int hartMainNC(HostBlock & hb, GN & gn) {
    return ++wastoid;
  }

  void blowupCodeSpace() {
    volatile int i;
    i = i*3+1;
#if 1
#define X0 i = i*3+1;
#define X1 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0
#define X2 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1
#define X3 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2
    X2
#undef X
#undef X1
#undef X2
#undef X3
#endif
  }
  int hartmain(uint32_t hartnum, HostBlock& hb) {
    blowupCodeSpace();
    switch (hartnum) {
    case 0u: return hartMainB(hb,gb);
    case 1u: return hartMainT0(hb,g0);
    case 2u: return hartMainT1(hb,g1);
    case 3u: return hartMainT2(hb,g2);
    case 4u: return hartMainNC(hb,gn);
    }
    return 0;
  }
}

extern "C" {
  int hartmain(uint32_t hartnum, MFM::HostBlock* hb) {
    return MFM::hartmain(hartnum, *hb);
  }
}
