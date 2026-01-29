#include "FastLocal.h"
#include "FastB.h"
#include "LiveB.h"
#include "Printf.h"

namespace MFM {

  int hartMainB(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_B);
    //    XXX_DEBUG_FUNC(__FILE__,__LINE__);

    //DP.printf("B#%d(%d,%d)\n",hb.mTLBI,hb.mPos.x,hb.mPos.y);

    // hb.mCommonArgs[0] reserved for nonce (used by T2)
    // hb.mCommonArgs[1] reserved for start decay type
    // hb.mCommonArgs[2] = (u32) hb.mHostBaseAddrHi; //ET_NIU_NODE_ID;
    //hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop

    XXX_DEBUG_FUNC(__FILE__,__LINE__);
    return liveB(hb);          // go do your hart B thing you
  }
}
