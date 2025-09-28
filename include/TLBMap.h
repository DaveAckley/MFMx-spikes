#ifndef TLBMAP_H          /* -*- mode: C++ -*- */
#define TLBMAP_H

#include "umd/device/pci_device.hpp"
#include "umd/device/tt_device/tt_device.h"

#include "umd/device/tt_core_coordinates.h"
#include "umd/device/blackhole_implementation.h"

#include "Util.h"

namespace MFM {

  class TLBMap {
  public:
    TLBMap()
      : mMapInitted(false)
    {
      memset(mMap,0u,sizeof(mMap));
    }

    void initMap(tt::umd::TTDevice * device) ;

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
    static const u32 MAX_X_COORD = 16u;
    static const u32 MAX_Y_COORD = 11u;
    typedef u8 XY2TLB[MAX_X_COORD+1u][MAX_Y_COORD+1u];
    XY2TLB mMap;
    bool mMapInitted;
  };

} // namespace MFM

#endif /* TLBMAP_H */
