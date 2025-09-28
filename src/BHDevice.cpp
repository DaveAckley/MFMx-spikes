#include <cassert>
#include <fstream>
#include "BHDevice.h"

namespace MFM {
  BHDevice::BHDevice(u32 did)
    : mDevNum(did)
    , mHasDevNum(true)
    , mCodeDeployed(false)
    , mDeployedCodeSize(0u)
    , mTTDevice(tt::umd::TTDevice::create(mDevNum))
  {
    printf("BHDevice%d says hullo\n",mDevNum);
  }


  BHDevice::BHDevice(BHDevice && oth)
    : mDevNum(oth.mDevNum)
    , mHasDevNum(true)
    , mTTDevice(std::move(oth.mTTDevice))
  {
    printf("BHDevice%d says copy %p<-%p\n",mDevNum,mTTDevice.get(),oth.mTTDevice.get());
  }

  BHDevice::~BHDevice() {
    printf("%s%d %p\n",__FUNCTION__,mDevNum,mTTDevice.get());
  }

  const char * BHDevice::getDeviceName() const
  {
    static char buf[100];
    snprintf(buf,100,"BHDevice%d",mDevNum);
    return buf;
  }

  tt::umd::TTDevice* BHDevice::getTTDPtr() {
    tt::umd::TTDevice * ptr = mTTDevice.get();
    assert(ptr);
    return ptr;
  }

  tt::umd::TTDevice& BHDevice::getTTD() {
    tt::umd::TTDevice * ptr = mTTDevice.get();
    assert(ptr);
    return *ptr;
  }

  double BHDevice::getDeviceTemperature() {
    return getTTD().get_asic_temperature();
  }

  s32 BHDevice::deployRISCVCodeFromFile(const char * path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); // Open in binary mode and at end

    if (!file.is_open()) 
      FATAL("Failed to open file: %s", path);

    std::streamsize rvCodeSize = file.tellg(); 
    file.seekg(0, std::ios::beg); // Seek back to beginning

    auto rvcode = std::make_unique<char[]>(rvCodeSize);

    if (!file.read(rvcode.get(), rvCodeSize))
      FATAL("Failed to read file: %s", path);

    printf(" RVCODE %s: ", path);

    file.close();

    return deployThisRISCVCode(rvcode.get(),rvCodeSize);
  }

  s32 BHDevice::deployThisRISCVCode(const char * rvcode, u32 rvsize) {
    printf(" BHD/LENGTH=%d (0x%08x, 0x%08x, ..., 0x%08x, 0x%08x)\n",
           rvsize,
           ((u32*) rvcode)[0],
           ((u32*) rvcode)[1],
           ((u32*) rvcode)[rvsize/4-2],
           ((u32*) rvcode)[rvsize/4-1]
           );

    // Deploy this RISCV machine code to the Tensix tile.
    getTTD().write_block(0u, rvsize, (const u8 *) rvcode);
    /*
    memcpy(set_tlb_addr(0), rvcode, rvsize);
    s32 c = memcmp(set_tlb_addr(0), rvcode, rvsize);
    if (0 != c)
      FATAL("CODE DEPLOY CHECK MISMATCH (%c)",c);
    else
      printf(" Code verified, %d bytes\n", rvsize);
    */
    mCodeDeployed = true;
    mDeployedCodeSize = rvsize;
    return 0;
  }


}
