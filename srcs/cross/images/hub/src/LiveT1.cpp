/* hub/LiveT1: Run compression only */
#include "LiveT1.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "Debug.h"
#include "EP_ACacheBlock.h"
#include "HartTasks.h"

namespace MFM {
  
  // L1 DATA
  LZBuf l1dLZBytesIn;
  LZBuf l1dLZBytesOut;

  static s32 uncompressedByteSource(bool isReadable, void * context) {
    static u32 calls = 0;
    RCFlag f = RCFlag::RC_ZERO;
    if ((++calls & 0x3'ffff) == 0) {
      theHostBlock.hartbeat(fAll.mHartNum);
      LOGXTAG(ubsCalls,calls);
      if ((calls & 0xf'ffff) == 0)
        HBXTAG(ubsCallsh,calls+(u32) f);
    }
    //    LOGMARK;
    //    if (isReadable) return l1dLZBytesIn.isEmpty() ? -2 : 0;
    if (isReadable) return 0;   // never return eof (0 not a real byte here)
    //    LOGMARK;
    u8 byte;
    if (l1dLZBytesIn.remove(byte)) {
      SNAP(100,LOGXTAG(UBSZONG,(u32) byte));
      return (s32) byte;
    }
    //    LOGMARK;
    return -2; // never returns eof?
  }
  static bool compressedByteSink(u8 byte, void * context) {
    MFM_API_ASSERT_NONNULL(context);
    ACacheBlockL1Control & acbl1 = *(ACacheBlockL1Control*) context;
    bool ret = acbl1.writeByteToCurrentACB(byte);
    if (!ret) SNAP(100,LOGXTAG(cBSBLK,(u32)byte));
    return ret;
  }

  struct FastT1 {
    lzmfmx mLZComp;
    void init() {
      HBMARK;
      l1dLZBytesIn.init();
      l1dLZBytesOut.init();

      mLZComp.init(uncompressedByteSource,0,compressedByteSink,&theACacheBlockL1Control);

#if 1

      u32 spin = 0;
      ACacheBlockL1Control & acbl1 = theACacheBlockL1Control;
      while (!acbl1.testFlags(acbl1.ACBL1_T0_INITTED)) { // wait for t0
        waitALittle();
        if ((++spin % 10'000) == 0)
          LOGPTAG(zwait,spin);
      }
#endif
      HBNOTE(PastT0InitOK);
      LOGPTAG(spunOut,spin);
      acbl1.setFlags(acbl1.ACBL1_T1_INITTED);
      HBNOTE(PastT1InitOK);
    }
    void live() {
      HBMARK;
      mLZComp.compressForever();
    }
  };
  FAST_LOCAL(FastT1,fT1,1);
  
  int initT1() {
    fT1.init();
    HBNOTE("initT1OK");
    //    HBMARK;
    return 0;
  }

  int liveT1(HostBlock & hb) {
    HBMARK;
    LOGMARK;
    fT1.live();
    return 0;
  }
}
