#include "DefaultLives.h"
#include "FastLocal.h"
#include "Debug.h"

namespace MFM {
  int doDefaultInit(HostBlock & hb) {
    LOGMARK;

    u8 ch = hartChar(fAll.mHartNum);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::INITTING; 
    switch (ch) {
    case 'b': initB(); break;
    case '0': initT0(); break;
    case '1': initT1(); break;
    case '2': initT2(); break;
    case 'n': initNC(); break;
    }

    LOGNOTE(GRAND);
    u32 WAIT_ITERATIONS = 150u;
    if (ch == 'n') WAIT_ITERATIONS = 1; // DEBUG: short circuit wait on HN
    for (u32 w = WAIT_ITERATIONS; w > 0u; --w) {
      //if ((w&0x1f) == 1u) HBPTAG(P:,w);
      hb.hartbeat(fAll.mHartNum);
      breathe();
    }
    //    HBNOTE("ENGAGE");
    return 0;
  }

  static int justLive(HostBlock & hb) {
    //    HBPTAG(JSTLV,getNameFromImageCode((ImageCode) fAll.mIBH.mImageCode));

    u8 ch = hartChar(fAll.mHartNum);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; 
    u16 spin = 0u;
    while (true) {
      if (spin++ == 0) hb.hartbeat(fAll.mHartNum);
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

  static int weakInit() {
    HBPTAG(weakInit,hartName(fAll.mHartNum));
    return 0;
  }

  int __attribute__((weak)) initB() { return weakInit(); }
  int __attribute__((weak)) initT0() { return weakInit(); }
  int __attribute__((weak)) initT1() { return weakInit(); } // NOTE FastT1.cpp has a strong initT2 for logging
  int __attribute__((weak)) initT2() { return weakInit(); } // NOTE FastT2.cpp has a strong initT2 for PRNG
  int __attribute__((weak)) initNC() { return weakInit(); }

  int __attribute__((weak)) liveB(HostBlock & hb) { return justLive(hb); }
  int __attribute__((weak)) liveT0(HostBlock & hb) { return justLive(hb); } // NOTE FastT0.cpp has a strong liveT0
  int __attribute__((weak)) liveT1(HostBlock & hb) { return justLive(hb); }
  int __attribute__((weak)) liveT2(HostBlock & hb) { return justLive(hb); } // NOTE FastT2.cpp has a strong liveT2
  int __attribute__((weak)) liveNC(HostBlock & hb) { return justLive(hb); }

  int __attribute__((weak)) stepB(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepT0(HostBlock & hb) { return 0; } // NOTE FastT0.cpp has a strong stepT0
  int __attribute__((weak)) stepT1(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepT2(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepNC(HostBlock & hb) { return 0; } // NOTE FastNC.cpp has a strong stepNC
  
}
