#include "itype.h"

namespace MFM {
  union D {
    struct GB { u32 clams; } gb;
    struct G0 { bool bong; } g0;
    struct G1 { s32 snig; u32 snog; u32 snug; } g1;
    struct G2 { } g2;
    struct GN { } gn;
  } d __attribute__ ((section(".fastram"), aligned(0x4)));

  int getThing() {
    return d.g1.snog + d.g1.snug;
  }

  int dkngflkd = -1;
}

extern "C" {
  static MFM::s32 foo = 88;
  int borgo;
  int bar() {
    if (foo>50) return 1;
    ++borgo;
    if (borgo != 1) return 2;
    return 0;
  }

  struct HostBlock {
    uint32_t mPerRiscArg[5];
    uint32_t mCommonArgs[3];
  } hb __attribute__ ((section(".hostblock")));
  
  int MAIN() {
    foo = MFM::getThing();
    return bar();
  }
}
