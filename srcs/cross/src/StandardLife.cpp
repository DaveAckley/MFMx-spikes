#include "StandardLife.h"
#include "FastLocal.h"
#include "Debug.h"
#include "T6BoltResponder.h"

namespace MFM {
  
  bool beatTheStandardHeartbeat(HostBlock & hb) {
    // HEARTBEAT AND BOLT RESPONSE
    u32 oldspin = fAll.mHeartSpin++;
    if ((oldspin & 0x3f) == 0) {
      theT6BoltResponder.boltResponderAllHarts();
      if (oldspin == 0) {
        hb.hartbeat(fAll.mHartNum);
        return true;
      }
    }
    return false;
  }

  void liveTheDefaultStandardLife(HostBlock & hb) {
    HBPTAG(JSTLV,getNameFromImageCode((ImageCode) fAll.mImageCode));

    u8 ch = hartChar(fAll.mHartNum);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 

    while (true) {
      beatTheStandardHeartbeat(hb);
      switch (ch) {
      case 'b': stepB(hb); break;
      case '0': stepT0(hb); break;
      case '1': stepT1(hb); break;
      case '2': stepT2(hb); break;
      case 'n': stepNC(hb); break;
      }
    }
  }

  void __attribute__((weak)) liveTheStandardLife(HostBlock & hb) { liveTheDefaultStandardLife(hb); }
  void __attribute__((weak)) stepB(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepT0(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); } // NOTE FastT0.cpp has a strong stepT0
  void __attribute__((weak)) stepT1(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepT2(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepNC(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); } // NOTE FastNC.cpp has a strong stepNC
  
}
