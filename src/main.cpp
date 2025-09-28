#include <stdio.h>
#include "BHUMD.h"

namespace MFM {
  s32 TestDevice(u32 device) {
    printf("BKAKDOGNA\n");
    MFM::BHUMD umd(device);           // talk to 0
    s32 ret;
    ret = umd.open();
    if (ret) return ret;

    umd.deployRISCVCode();
    umd.doTests();

    ret = umd.close();
    return ret;
  }
}

int main(int argc, const char** argv) {
  for (MFM::u32 i = 0u; i < 4u; ++i) {
    MFM::TestDevice(i);
  }
  return 0;
}
