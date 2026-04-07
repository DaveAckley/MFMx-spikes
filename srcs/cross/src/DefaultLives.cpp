#include "DefaultLives.h"
#include "FastLocal.h"

namespace MFM {
  static int justLive(HostBlock & hb) {
    hb.packString("JUSTLIVE");
    u8 ch = hartChar(fAll.mHartNum);
    hb.addBytes(':',ch);
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

  int __attribute__((weak)) liveB(HostBlock & hb) { return justLive(hb); }
  int __attribute__((weak)) liveT0(HostBlock & hb) { return justLive(hb); } // NOTE FastT0.cpp has a strong liveT0
  int __attribute__((weak)) liveT1(HostBlock & hb) { return justLive(hb); }
  int __attribute__((weak)) liveT2(HostBlock & hb) { return justLive(hb); } // NOTE FastT2.cpp has a strong liveT2
  int __attribute__((weak)) liveNC(HostBlock & hb) { return justLive(hb); }

  int __attribute__((weak)) stepB(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepT0(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepT1(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepT2(HostBlock & hb) { return 0; }
  int __attribute__((weak)) stepNC(HostBlock & hb) { return 0; }
  
}
