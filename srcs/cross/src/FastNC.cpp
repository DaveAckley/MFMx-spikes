#include "FastNC.h"
#include "FastLocal.h"
#include "TransportBlock.h"
#include "Printf.h"
#include "NoCs.h"
#include "S8C.h"

namespace MFM {
  T6ElevatorTransport theT6ElevatorTransport;

  LogCarStorage theLogCarStorage __attribute__ ((section(".transportblocklog")));
  EWCarStorage theEWCarStorage __attribute__ ((section(".transportblockew")));

  TransportBlock theTransportBlock __attribute__ ((section(".transportblock"))) = {
    .mLogCarStorageT6Ptr = (u32) &theLogCarStorage,
    .mEWCarStorageT6Ptr = (u32) &theEWCarStorage,
  };

  struct FastNC {
    void init() { mNoCs.init(); }
    NoCs mNoCs;
  };
  FAST_LOCAL(FastNC,fNC,nc);

  static int liveNC(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveNC(HostBlock & hb) {
    u8 spin = 0u;
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      theT6ElevatorTransport.updateTransportBlock();
      hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
      sleepCycles(1'000u);
    }
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    theT6ElevatorTransport.init(hb,theTransportBlock);
    fNC.init(); // ctors don't run for objects in our fast RAMs!
    if (false) {
      U8C xy0 = fNC.mNoCs.mNoC0.mNoCXY;
      U8C xy1 = fNC.mNoCs.mNoC1.mNoCXY;
      DP.printf("NC#%d(%d,%d)x(%d,%d)\n",
                hb.mTLBI,
                xy0.x, xy0.y,
                xy1.x, xy1.y);
      for (u32 n = 0u; n < 2u; ++n)
        for (u32 i = 0u; i < 4u; ++i)
          DP.printf("NR%d.%d:0x%08x\n",n, i,(u32) fNC.mNoCs.getNoC(n).getNIUReqAddress(0u, i));
    }
    {
      for (u8 d = Dir8::NT; d <= Dir8::NE; ++d) {
        S8C s = S8C::makeS8CFromDir8((Dir8) d);
        DP.printf("DIR%d=(%d,%d)\n", d, s.x, s.y);
      }
    }
    
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveNC(hb);

    return 0; /* NOT REACHED */
  }
}

