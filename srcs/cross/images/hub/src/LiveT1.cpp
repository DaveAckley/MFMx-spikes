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
      //LOGXTAG(ubsCalls,calls);
      if ((calls & 0xf'ffff) == 0)
        HBXTAG(ubsCallsh,calls+(u32) f);
    }
    //    LOGMARK;
    //    if (isReadable) return l1dLZBytesIn.isEmpty() ? -2 : 0;
    if (isReadable) return 0;   // never return eof (0 not a real byte here)
    //    LOGMARK;
    //    EACH(100'000,LOGPTAG(unBS,__EACHNUM__));
    u8 byte;
    if (l1dLZBytesIn.remove(byte)) {
      //      SNAP(20,LOGXTAG(UBSZONG,(u32) byte));
      return (s32) byte;
    }
    //    LOGMARK;
    return -2; // never returns eof?
  }
  static bool compressedByteSink(u8 byte, void * context) {
    MFM_API_ASSERT_NONNULL(context);
    ACacheBlockL1Control & acbl1 = *(ACacheBlockL1Control*) context;
    bool ret = acbl1.writeByteToCurrentACB(byte);
    if (!ret) SNAP(3,LOGXTAG(cBSBLK,(u32)byte));
    return ret;
  }

  struct FastT1 {
    lzmfmx mLZComp;
    void init() {
      HBPTAG(fT1,sizeof(FastT1));
      l1dLZBytesIn.init();
      l1dLZBytesOut.init();
      HBMARK;
      mLZComp.init(uncompressedByteSource,0,compressedByteSink,&theACacheBlockL1Control);
      HBMARK;
      HBNOTE(PastT1InitOK);
      HBNOTE(WOTNOWBATMAN);
      theACacheBlockL1Control.setFlags(theACacheBlockL1Control.ACBL1_T1_INITTED);
    }
    void live() {
      HBMARK;
      mLZComp.compressForever();
    }
  };
  FAST_LOCAL(FastT1,fT1,1);
  
  int myInitT1() {
    fT1.init();
    HBNOTE("initT1OK");
    //    HBMARK;
    return 0;
  }

  int myLiveT1(HostBlock & hb) {
    HBMARK;
    LOGMARK;
    fT1.live();
    return 0;
  }

  int initT1(HostBlock & hb) { return myInitT1(); }
  int liveT1(HostBlock & hb) { return myLiveT1(hb); }
}
