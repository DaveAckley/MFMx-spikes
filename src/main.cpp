#include <stdio.h>
#include "BHUMD.h"

namespace MFM {
  s32 TestDevice(u32 device) {
    printf("\nBUD13MAN %d\n",device);
    MFM::BHUMD umd;
    umd.setDeviceNumber(device); 
    s32 ret;
    ret = umd.open();
    if (ret) return ret;

    printf("DEPLOYING CODE\n");
    umd.deployRISCVCodeFromFile("./cross/bin/test10.bin");

    printf("RELEASING THE HOUNDS\n");
    umd.releaseTheHounds();

    printf("CLOSING UP\n");
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
