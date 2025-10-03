#include "itype.h"
#include "utils.h"
#include "RandMT.h"
#include "HostBlock.h"
#include "AtomicLock.h"
#include "Constants.h"
#include "ExtraConstants.h" // for NOC_NODE_ID0
#include "P2PElevator.h"
#include "T6ElevatorTransport.h"
#include "TransportBlock.h"
#include "Fail.h"
#include "Printf.h"
#include "FastLocal.h"

namespace MFM {
  HostBlock theHostBlock __attribute__ ((section(".hostblock"))) = {
    .mHBMagic = HostBlock::HBMAGIC+1,
    .mXPos = U8_MAX,
    .mYPos = U8_MAX-1u,
    .mTLBI = U8_MAX-2u,
    .mHBCigam = HostBlock::HBCIGAM+1
  };

  /*
  char theTransportBlock[0x200] __attribute__ ((section(".transportblock"))) =
    "hi there I'm The Transport Block! I'm very important!"
  ;
  */
  TransportBlock theTransportBlock __attribute__ ((section(".transportblock")));

#if 0
  struct GB { u32 mHartNum; u32 clams; };
  struct G0 { u32 mHartNum; bool bong; };
  struct G1 { u32 mHartNum; s32 snig; u32 snog; u32 snug; };
  struct G2 { u32 mHartNum; RandMT mRandom; };
  struct GN { u32 mHartNum; };

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
#endif
  
  u32 wastoid;
  AtomicLock mylock;  // static -> can't hold shared locks in private RAM

  /*
  struct DemoCar {
    u8 mBytes[256];
  };
  typedef P2PElevatorPlatform<DemoCar,2> MyPlatform;
  MyPlatform myPlatform;
  */

  //  int blowupCodeSpace(int) ;

  int hartMainB(HostBlock & hb) {
    theHostBlock.mHBMagic = 0xc0ded0d0; // DOES NOT KILL IT
    //    hb.mHBMagic = 0xb00f00;
    hb.mLogBuffer.add('B');
    hb.mLogBuffer.add('*');
    DP.printf("B:HI!");
    //    hb.mCommonArgs[0] = node_id;
    hb.mCommonArgs[1] = (u32) hb.mHostBaseAddrLo; //ET_NIU_BASE;
    hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;
    //hb.mCommonArgs[1] = node_endpoint_id;
    hb.addString("hart B: lock test\n");
    mylock.acquireLock();
    DP.printf("hart B: i hold the test lock 0x%x\n",hb.mCommonArgs[1]);
    //hb.addString("hart B: i hold the test lock\n");
    if (!mylock.tryLock())
      DP.printf("hart B: and i can't take it again because i already have it doh\n");
    mylock.releaseLock();
    DP.printf("hart B: lock released\n");
    return 0;
  }
  int hartMainT0(HostBlock & hb) {
    theHostBlock.mHBMagic = 0xc0ded0d1; // DOES NOT KILL IT
    //    hb.mHBMagic = 0xb00f1;
    DP.printf("0:HI!");
    ++wastoid;
    return 0;
  }
  int hartMainT1(HostBlock & hb) {
    theHostBlock.mHBMagic = 0xc0ded0d2; // KILLS BY ITSELF
    DP.printf("1:HI!");
    if (wastoid > 3) --wastoid;
    //theHostBlock.mHBMagic = 0xc0ded0d7; // BLOCKS OTHERS KILLING
    return 0;
  }
  int hartMainT2(HostBlock & hb) {

    //theHostBlock.mHBMagic = 0xc0ded0d3; // DOES KILL IT
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    //    hb.mHBMagic = 0xb00f3;

    DP.printf("2:HI!");
    fT2.mRandom.seedMT_MFM(2);
    hb.addString("hart T2 report: Goodbye World\n");
    return fT2.mRandom.randomMT() & 0xffffff;
  }

  int hartMainNC(HostBlock & hb) {
    //theHostBlock.mHBMagic = 0xc0ded0d4; // DOES KILL IT
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    fNC.mT6ElevatorTransport.init(hb,theTransportBlock);
    hb.mLogBuffer.add('N');
    hb.mLogBuffer.add('C');
    DP.printf("NC:HI!");
    for (u32 i = 0u; i < 2u; ++i)
      theTransportBlock.mLogCars[i].mHeader.mStatus = CarHeader::CH_OPEN;
    TransportBlock::LogCar * b1 = &theTransportBlock.mLogCars[0];
    if (b1 != 0) {
      DP.printf("HIO!");
      u32 * udata = (u32*) &b1->u;
      u32 usize = sizeof(b1->u)>>2;
      for (u32 i = 0u; i < usize; ++i)
        udata[i] = 0xf00baa9;
      u64 destbase = (((u64) hb.mHostBaseAddrHi)<<32) + hb.mHostBaseAddrLo;
      //      if (hb.mTLBI > 0) hb.mHBCigam= udata[1];

      u64 destaddr = destbase + hb.mTLBI * sizeof(TransportBlock);
      u64 pcietlbaddr = destaddr;  //  | 0x1000'0000'0000'0000; // to outbound iatu then host iommu?
      if ((pcietlbaddr%16) != 0)   // nice round addrs right?
        FAIL(BAD_ALIGNMENT);
      else if (0!=fNC.mT6ElevatorTransport.initiateWriteToHost(udata,usize,pcietlbaddr))
        FAIL(OPERATION_FAILED);
    } else
      hb.mHBCigam= -1; // blow the sig as a signal?
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    while (false)
      fNC.mT6ElevatorTransport.updateTransportBlock();
    return 0; /* NOT REACHED */
  }

  int t6setup(HostBlock &hb) {
    u32 node_id = *NOC_NODE_ID0;
    hb.mXPos = ((node_id >> 0) & 0x3f);
    hb.mYPos = ((node_id >> 6) & 0x3f);
    hb.mTLBI = U16C::makeTLBIFromNocCoord({hb.mXPos,hb.mYPos});
    return 0;
  }

  int hartmain(HostBlock& hb) {
    switch (fAll.mHartNum) {
    case 0u: return hartMainB(hb);
    case 1u: return hartMainT0(hb);
    case 2u: return hartMainT1(hb);
    case 3u: return hartMainT2(hb);
    case 4u: return hartMainNC(hb);
    }
    return 0;
  }

#if 0
  int blowupCodeSpace(int j) {
    static int i = 0;
#if 1
#define X0 i = i*3+j;
#define X1 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0 X0
#define X2 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1 X1
#define X3 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2 X2
    X0
#undef X0
#undef X1
#undef X2
#undef X3
#endif
    return i;
  }
#endif
}

extern "C" {
  int t6setup(MFM::HostBlock *hb) {
    return MFM::t6setup(*hb);
  }
  int hartmain(MFM::HostBlock* hb) {
    //    hb->mHBMagic += 20; // BLOW IT
    return MFM::hartmain(*hb);
  }
}
