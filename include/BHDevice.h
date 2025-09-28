#ifndef BHDevice_H          /* -*- mode: C++ -*- */
#define BHDevice_H

#include "Util.h"
#include "RandMT.h"
#include "umd/device/pci_device.hpp"
#include "umd/device/tt_device/tt_device.h"

namespace MFM {

  /** a single blackhole device */
  class BHDevice {
  public:

    BHDevice(u32 did) ;
    BHDevice(BHDevice && oth) ;
    ~BHDevice() ;

    const char * getDeviceName() const ;
    double getDeviceTemperature() ;
    u32 getCurClockFreq() { return getTTD().get_clock(); }
    u32 getMaxClockFreq() { return getTTD().get_max_clock_freq(); }
    u32 getMinClockFreq() { return getTTD().get_min_clock_freq(); }

    void setDeviceNumber(u32 deviceNumber) ;
    bool unsetDeviceNumberIfSet() ;

    void setTilePosition(u32 xpos, u32 ypos) ;
    void setRandomTilePosition(RandMT & rmt) ;

    s32 open() ;
    s32 close() ;

    s32 deployRISCVCodeFromFile(const char * path) ;
    s32 deployThisRISCVCode(const char * rvcode, u32 rvsize) ;

    s32 releaseTheHounds() ;
    s32 waitTilDone() ;


  private:
    u32 mDevNum;
    bool mHasDevNum;
    bool mCodeDeployed;
    u32 mDeployedCodeSize;

  public:
    tt::umd::TTDevice* getTTDPtr() ;
    tt::umd::TTDevice& getTTD() ;
    std::unique_ptr<tt::umd::TTDevice> mTTDevice;

#if 0
    s32 open_bh_pcie_device() ;
    s32 close_bh_pcie_device() ;

    s32 configure_tlb() ;
    s32 set_tlb_xy() ; // HERE'S WHERE (CURRENTLY SINGLE) TENSIX CORE IS SELECTED
    char* set_tlb_addr(u64 addr) ;

    void tlb_write_u32(u64 addr, u32 value) ;
    u32 tlb_read_u32(u64 addr) ;

    bool mDevOpen;

    u32 mTileXPosition;
    u32 mTileYPosition;

    bool mCodeDeployed;
    u32 mDeployedCodeSize;

    //bh_pcie_device_t mBHDev;
#endif
  };

}
#endif /*  BHDevice_H */
