/* hub/LiveT1: Run compression only */
#include "LiveT1.h"
#include "HostBlock.h"
#include "FastLocal.h"
#include "Debug.h"
#include "EP_ACacheBlock.h"
#include "HartTasks.h"
#include "T6Phaser.h"
#include "StandardLife.h" // for liveTheDefaultStandardLife

namespace MFM {
  
  // L1 DATA
  LZBuf l1dLZBytesIn;
  LZBuf l1dLZBytesOut;

  static s32 uncompressedByteSource(bool isReadable, void * context) {
    static u32 calls = 0;
    RCFlag f = RCFlag::RC_ZERO;
    if ((++calls & 0x3'ffff) == 0) {
      theHostBlock.hartbeat(fAll.mHartNum);
      if ((calls & 0xf'ffff) == 0)
        HBXTAG(ubsCallsh,calls+(u32) f);
    }

    if (isReadable) return 0;   // never return eof (0 not a real byte here)
    u8 byte;
    if (l1dLZBytesIn.remove(byte)) {
      return (s32) byte;
    }
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

  void liveTheStandardLife(HostBlock & hb) { // HUB has a custom life for T1!
    if (fAll.mHartNum != HARTNUM_T1) return liveTheDefaultStandardLife(hb);
    HBNOTE(CUSTOMLIFE);
    MFM_API_ASSERT_ON_HART(HARTNUM_T1); // as documentation

    // Mark us in business
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 

    HBMARK;
    LOGMARK;
    fT1.live();
  }  

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
