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
    .mHBMagic = HostBlock::HBMAGIC,
    .mXPos = U8_MAX,
    .mYPos = U8_MAX-1u,
    .mTLBI = U8_MAX-2u,
    .mHBCigam = HostBlock::HBCIGAM
  };

  TransportBlock theTransportBlock __attribute__ ((section(".transportblock")));

  u32 wastoid;
  AtomicLock mylock;  // static -> can't hold shared locks in private RAM

  int hartMainB(HostBlock & hb) {
    DP.printf("HI from %d (%d,%d)\n",hb.mTLBI,hb.mXPos,hb.mYPos);
    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    hb.mCommonArgs[1] = (u32) hb.mHostBaseAddrLo; //ET_NIU_BASE;
    hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;

    mylock.acquireLock();
    DP.printf("hart B: i hold the test lock 0x%x\n",hb.mCommonArgs[1]);
    if (!mylock.tryLock())
      DP.printf("hart B: and i can't take it again because i already have it doh\n");
    mylock.releaseLock();
    DP.printf("hart B: lock released\n");
    return 0;
  }
  int hartMainT0(HostBlock & hb) {
    DP.printf("%d:HI![%d] ",fAll.mHartNum,++wastoid);
    return 0;
  }
  int hartMainT1(HostBlock & hb) {
    DP.printf("%d:Hoo[%d] ",fAll.mHartNum,++wastoid);
    return 0;
  }
  int hartMainT2(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T2);
    
    DP.printf("%d:rep[%d] ",fAll.mHartNum,wastoid);
    u32 seed = hb.mCommonArgs[0] * (hb.mXPos+1) + (hb.mYPos);
    fT2.mRandom.seedMT_MFM(seed);
    DP.printf("hart T2 report: Goodbye World (0x%08x)\n",
              fT2.mRandom.randomMT());
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    //theHostBlock.mHBMagic = 0xc0ded0d4; // DOES KILL IT
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    fNC.mT6ElevatorTransport.init(hb,theTransportBlock);
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
