#include "FastNC.h"
#include "FastLocal.h"
#include "TC.h"
#include "CellBlock.h"
//#include "T6Grid.h"
#include "Printf.h"
#include "CrossUtils.h"
//#include "NoCs.h"
#include "S8C.h"
//#include "FastT2.h" // REMEMBER: NO createByMail on HART NC!

namespace MFM {
  //  T6ElevatorTransport theT6ElevatorTransport;

#if 0
  LogCarStorage theLogCarStorage[1] __attribute__ ((section(".transportblocklog")));
  EWCarStorage theEWCarStorage __attribute__ ((section(".transportblockew")));

  TransportBlock theTransportBlock __attribute__ ((section(".transportblock"))) = {
    .mLogCarStorageT6Ptr = (u32) &theLogCarStorage,
    .mEWCarStorageT6Ptr = (u32) &theEWCarStorage,
  };
#endif
  CellBlock theCellBlock[1] __attribute__ ((section(".crossrodata")));

  struct FastNC {
    void init() { }
    //NoCs mNoCs;
  };
  FAST_LOCAL(FastNC,fNC,nc);

  //static int liveNC(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveNC(HostBlock & hb) {
    hb.addByte('N');
    u16 spin = 0u;
    while (true) {
      if (++spin == 0) {
        hb.hartbeat(fAll.mHartNum);
      }

      //theT6ElevatorTransport.updateTransportBlock();
    
      hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
      sleepCycles(1'000u);
    }
    return 0;
  }

  int hartMainNC(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    //theT6ElevatorTransport.init(hb,theTransportBlock);
    fNC.init(); // ctors don't run for objects in our fast RAMs!
    /*
    NoCHop h;
    Dir4 d4 = (Dir4) ((hb.mPos.x+hb.mPos.y)&0x3);
    if (h.initForNextT6InDir4(d4,hb.mPos))
      DP.printf("NC@%d(%u,%u)%c:(%u,%u)!\n",
                hb.mTLBI,
                h.mSrcCoord.x,
                h.mSrcCoord.y,
                dir4ToByteCode(d4),
                h.mDestCoord0.x,
                h.mDestCoord0.y
                );
    else
      DP.printf("NC@(%u,%u)%c:NO!\n",
                hb.mPos.x,hb.mPos.y,
                dir4ToByteCode(d4)
                );
    */
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    return liveNC(hb);

    return 0; /* NOT REACHED */
  }
}

