#include <stdio.h>
#include "BHUMD.h"
#include "RandMT.h"

int main(int argc, const char** argv) {
  MFM::RandMT rmt;
  sleep(1);
  rmt.seedMT_MFM(time(0));

  MFM::BHUMD devices[4];
  const MFM::u32 dcount = 4u;
  int ret;
  for (MFM::u32 i = 0u; i < dcount; ++i) {
    MFM::BHUMD & umd = devices[i];

    umd.setDeviceNumber(i);
    umd.setRandomTilePosition(rmt);

    printf("%s OPENING\n",umd.getDeviceName());
    ret = umd.open();
    if (ret) return ret;
  }
  printf("\n");

  for (MFM::u32 i = 0u; i < dcount; ++i) {
    MFM::BHUMD & umd = devices[i];

    printf("%s DEPLOYING CODE\n",umd.getDeviceName());
    umd.deployRISCVCodeFromFile("./cross/bin/test10.bin");
  }
  printf("\n");
  
  for (MFM::u32 i = 0u; i < dcount; ++i) {
    MFM::BHUMD & umd = devices[i];
    printf("%s RELEASING THE HOUNDS\n",umd.getDeviceName());
    umd.releaseTheHounds();
  }
  printf("\n");
  for (MFM::u32 i = 0u; i < dcount; ++i) {
    MFM::BHUMD & umd = devices[i];
    printf("%s WAITING FOR RESULTS\n",umd.getDeviceName());
    umd.waitTilDone();
  }
  printf("\n");

  for (MFM::u32 i = 0u; i < dcount; ++i) {
    MFM::BHUMD & umd = devices[i];
    printf("%s CLOSING\n",umd.getDeviceName());
    ret = umd.close();
  }
  return 0;
}


