#include "TLBMap.h"

namespace MFM {

  static constexpr uint32_t NUM_PORTS_PER_DRAM_CHANNEL = 3;
  static constexpr uint32_t NUM_DRAM_CHANNELS = 8;
  // Values taken from blackhole.py in `src/t6ifc/t6py/packages/tenstorrent/chip/blackhole.py`
  static constexpr uint32_t ETH_STATIC_TLB_START = 0;
  static constexpr uint32_t TENSIX_STATIC_TLB_START = 38;

  static int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location =
      std::find(
                std::cbegin(tt::umd::blackhole::ETH_LOCATIONS), std::cend(tt::umd::blackhole::ETH_LOCATIONS), target) !=
      std::cend(tt::umd::blackhole::ETH_LOCATIONS);
    bool is_tensix_location =
      std::find(
                std::cbegin(tt::umd::blackhole::T6_X_LOCATIONS), std::cend(tt::umd::blackhole::T6_X_LOCATIONS), target.x) !=
      std::cend(tt::umd::blackhole::T6_X_LOCATIONS) &&
      std::find(
                std::cbegin(tt::umd::blackhole::T6_Y_LOCATIONS), std::cend(tt::umd::blackhole::T6_Y_LOCATIONS), target.y) !=
      std::cend(tt::umd::blackhole::T6_Y_LOCATIONS);
    // implementation migrated from blackhole.py in `src/t6ifc/t6py/packages/tenstorrent/chip/blackhole.py` from tensix
    // repo (t6py-blackhole-bringup branch)

    auto dram_tlb_index =
      std::find(tt::umd::blackhole::DRAM_LOCATIONS.begin(), tt::umd::blackhole::DRAM_LOCATIONS.end(), target);
    if (dram_tlb_index != tt::umd::blackhole::DRAM_LOCATIONS.end()) {
      return -1; // AHAX DON'T CONFIGURE DRAM TILES??
      /*
        auto dram_index = dram_tlb_index - tt::umd::blackhole::DRAM_LOCATIONS.begin();
        // We have 3 ports per DRAM channel so we divide index by 3 to map all the channels of the same core to the same
        // TLB
        return tt::umd::blackhole::TLB_BASE_INDEX_4G + (dram_index / NUM_PORTS_PER_DRAM_CHANNEL);
      */
    }

    // One row of BH ethernet cores starting at x = 1, y = 1
    //  and BH tensix cores are starting from x = 1, y = 2
    target.y--;
    target.x--;
    if (target.x >= 8) {
      target.x -= 2;
    }

    //TT_ASSERT(is_eth_location or is_tensix_location);
    int y = is_eth_location ? target.y : (target.y - 1);
    int flat_index = y * 14 + target.x;
    int tlb_index = (is_eth_location ? ETH_STATIC_TLB_START : TENSIX_STATIC_TLB_START) + flat_index;
    return tlb_index;
  }
  static s32 getTensixTLBIndex(tt_xy_pair target) {
    s32 ret = get_static_tlb_index(target);
    if (ret >= 0 && ret < tt::umd::blackhole::ETH_LOCATIONS.size())
      ret = -1;                 // knock out eth locations
    return ret;
  }

  void TLBMap::initMap(tt::umd::TTDevice * device) {
    if (!device) FATAL("NULL PTR");
    if (mMapInitted) FATAL("ALREADY INITTED");
    std::unique_ptr<tt::umd::TLBManager> tlb_manager = std::make_unique<tt::umd::TLBManager>(device);
    
    for (u32 y = 0u; y <= MAX_Y_COORD; ++y) {
      if (y < 2u) continue;
      for (u32 x = 0u; x <= MAX_X_COORD; ++x) {
        if (x == 8u) continue;
        tt_xy_pair targ(x,y);
        s32 tlbidx = getTensixTLBIndex(targ);
        if (tlbidx<0) continue;
        if (tlbidx==0) FATAL("WAA");

        tlb_manager->configure_tlb(targ, tlbidx, 0u, tt::umd::tlb_data::Relaxed);
        if (mMap[x][y] != 0u) FATAL("WAA?");
        mMap[x][y] = tlbidx;
      }
    }
    mMapInitted = true;
  }

}
