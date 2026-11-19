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

    // EOF only when T0 has asked for a stream restart (isDraining) and the
    // ring is empty.  T0 sets the flag AFTER its last byte is in the ring, so
    // test the flag first and the ring second (and re-try the ring once when
    // the flag is up): a byte can never be lost to the end of the stream.
    ACacheBlockL1Control & acl1 = theACacheBlockL1Control;
    if (isReadable) {           // the EOF test: cheap unless the ring is empty
      if (!l1dLZBytesIn.isEmpty() || !acl1.isDraining()) return 0;
      return l1dLZBytesIn.isEmpty() ? -1 : 0;   // flag seen: ring again
    }
    u8 byte;
    if (l1dLZBytesIn.remove(byte)) {
      return (s32) byte;
    }
    if (acl1.isDraining()) {
      if (l1dLZBytesIn.remove(byte)) return (s32) byte;
      return -1;                // the stream ends here
    }
    return -2;                  // blocked: nothing yet
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
      LOGMARK;
      ACacheBlockL1Control & acl1 = theACacheBlockL1Control;
      while (true) {
        mLZComp.encode();       // runs until T0 ends the stream (restart request)
        acl1.finishStream();    // last car EOF|CLOSABLE, next car SOF
        // Do not write a byte of the new stream into the old car: wait for T0
        // to ship it (it stamps the SOF on the next one, which clears this).
        while (acl1.sofPending()) {
          beatTheStandardHeartbeat(theHostBlock);
          waitALittle();
        }
        mLZComp.reset();        // ring and trees as at boot; the host does the same
        acl1.streamRestarted(); // T0 resumes feeding
      }
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
