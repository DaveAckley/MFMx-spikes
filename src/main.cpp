#include <stdio.h>
#include <iostream>
#include "BHUMD.h"
#include "BHHD.h"
#include "BHDevice.h"
#include "RandMT.h"

//#include "dispatch/memcpy.hpp"

namespace MFM {
  extern int ttmain();
  void dostuff() {
    printf("DOING STUFF\n");

    BHHD hostDriver;
    hostDriver.enumerate();

    for (BHDevice & d : hostDriver) {
      tt::umd::TTDevice* device = d.getTTDPtr();
      device->init_tt_device();

      std::cout << "Clock: " << device->get_clock() << " MHz" << std::endl;
      std::cout << "Board ID: 0x" << std::hex << device->get_board_id() << std::dec << std::endl;
      std::cout << "Temperature: " << device->get_asic_temperature() << "°C" << std::endl;
      
      std::cout << "ArcMessenger available: " << (device->get_arc_messenger() ? "Yes" : "No") << std::endl;
      std::cout << "ArcTelemetryReader available: " << (device->get_arc_telemetry_reader() ? "Yes" : "No")
                << std::endl;
      printf("I GOT %s (temperature=%f)\n",
             d.getDeviceName(),
             d.getDeviceTemperature());
      printf("SPEED min %d, cur %d, max %d\n",
             d.getMinClockFreq(),
             d.getCurClockFreq(),
             d.getMaxClockFreq());

      printf("%s DEPLOYING CODE\n",d.getDeviceName());
      d.deployRISCVCodeFromFile("./cross/bin/test10.bin");

#if 0      
      void* hugepage_base = 0x0; //wtfnose
      void* src_wtf = 0x0; //wtfnose
      
      uint32_t hugepage_size = 1000;
      uint32_t page_size = 4096;
      uint64_t hugepage_addr = reinterpret_cast<uint64_t>(hugepage_base);
      uint64_t hugepage_end = hugepage_addr + hugepage_size;
      uint64_t src_addr = reinterpret_cast<uint64_t>(src_wtf);
      tt::tt_metal::memcpy_to_device<false>((void*)(hugepage_addr), (void*)(src_addr), page_size);
#endif
    }

    //return hostDriver;
  }
}

int main(int argc, const char** argv) {
  MFM::RandMT rmt;
  sleep(1);
  rmt.seedMT_MFM(time(0));

  MFM::dostuff();

#if 0
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
  ///
  //  return MFM::ttmain();
#endif
  return 0;
}


