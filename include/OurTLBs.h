#ifndef OURTLBS_H          /* -*- mode: C++ -*- */
#define OURTLBS_H

#include "umd/device/pci_device.hpp"
#include "umd/device/tt_device/tt_device.h"

#include "umd/device/tt_core_coordinates.h"
#include "umd/device/blackhole_implementation.h"

#include "Util.h"

namespace MFM {

  class OurTLBs {
  public:
    static const u8 OurL1TLBIndex = 199u;
    static const u8 OurDebugTLBIndex = 200u;
    
    OurTLBs(tt::umd::TTDevice * device)
    {
      if (!device) FATAL("NO TTDEVICE");
      mDevice = device;
      mTLBManager = std::make_unique<tt::umd::TLBManager>(mDevice);
      initTLBs();
    }

    void initTLBs() ;

    u8 getTLBIdxIfAny(u8 x, u8 y) {
      if (!mMapInitted) FATAL("NOT INITTED");
      if (x > MAX_X_COORD || y > MAX_Y_COORD)
        return 0u;
      return mMap[x][y];
    }

    u8 getTLBIdx(u8 x, u8 y) {
      u8 ret = getTLBIdxIfAny(x,y);
      if (!ret) FATAL("No tlb for (%d,%d)",x,y);
      return ret;
    }

  private:
    tt::umd::TTDevice * mDevice;
    std::unique_ptr<tt::umd::TLBManager> mTLBManager;

    static const u32 MAX_X_COORD = 16u;
    static const u32 MAX_Y_COORD = 11u;
    typedef u8 XY2TLB[MAX_X_COORD+1u][MAX_Y_COORD+1u];
    XY2TLB mMap;
    bool mMapInitted;
  };

} // namespace MFM

#endif /* OURTLBS_H */
