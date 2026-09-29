#include "StandardLife.h"
#include "FastLocal.h"
#include "Debug.h"
#include "T6Phaser.h"

namespace MFM {
  
  int liveTheStandardLife(HostBlock & hb) {
    HBPTAG(JSTLV,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode));

    u8 ch = hartChar(fAll.mHartNum);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
    u16 spin = 0u;
    while (true) {
      if (spin++ == 0) hb.hartbeat(fAll.mHartNum);
      T6Phaser::handle();

      switch (ch) {
      case 'b': stepB(hb); break;
      case '0': stepT0(hb); break;
      case '1': stepT1(hb); break;
      case '2': stepT2(hb); break;
      case 'n': stepNC(hb); break;
      }
    }
    // NOT REACHED
    return 0;
  }

  void __attribute__((weak)) stepB(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepT0(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); } // NOTE FastT0.cpp has a strong stepT0
  void __attribute__((weak)) stepT1(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepT2(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); }
  void __attribute__((weak)) stepNC(HostBlock & hb) { FAIL(UNSUPPORTED_OPERATION); } // NOTE FastNC.cpp has a strong stepNC
  
}
