#include "FastLocal.h"
#include "FastT1.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox, createByMail
#include "Printf.h"
#include "T6RingO.h"
#include "T6Grid.h"

namespace MFM {
  struct FastT1 {
    T6RingOscillators mRingOs;
  };

  FAST_LOCAL(FastT1,fT1,t1);

  static int liveT1(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveT1(HostBlock & hb) {
    u64 spin = 0u;
    DP.printf("+T1(%d,%d)+\n",fAll.mPos.x,fAll.mPos.y);
    fT1.mRingOs.init();
    while (true) {
      ++spin;
      if ((spin % (1<<5u))==0) hb.hartbeat(fAll.mHartNum);
      if ((spin % (1<<24u))==0) {
        LOG.printf("%s(%d,%d) TLWW:%dK (%lldM)\n",
                   hartName(fAll.mHartNum),
                   fAll.mPos.x,fAll.mPos.y,
                   fT1.mRingOs.totalKWordsWritten(),
                   spin/(1<<20u));
      }
      ///// XXXXX DON'T DO RINGOS, FOR NOW FOR DEMO SPEED
      if (false)
        fT1.mRingOs.update();
    }
    return 0;
  }

  int hartMainT1(HostBlock & hb) {
    XXX_DEBUG_FUNC(__FILE__,__LINE__);
    //DP.printf("T1@(%u,%u)\n", hb.mPos.x, hb.mPos.y);
    MFM_API_ASSERT_ON_HART(HARTNUM_T1);
    preloadT2Mailbox();
    XXX_DEBUG_FUNC(__FILE__,__LINE__);
    DP.printf("CEW+%u,%d <%d:%08x>\n",
              sizeof(CornerEW),
              (s32) (offsetof(CornerEW,mFooter) - offsetof(CornerEW,mWaister)),
              fAll.mInspirationOnHand, fAll.mCreativityBuffer);
    DP.printf("T6G(%u,%u)=%d\n",T6GRID_WIDTH,T6GRID_HEIGHT,sizeof(T6Grid));
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    LOG.printf("%s:GO LIVE MAXSTAX %d\n",hartName(fAll.mHartNum),estimateStackUsage());
    return liveT1(hb);          // go do your hart t1 thing you
  }  
}
