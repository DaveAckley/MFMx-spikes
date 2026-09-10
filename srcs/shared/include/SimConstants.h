#pragma once /* -*- C++ -*- */

/** Constants associated with simulation business rather than hardware
    directly */
namespace MFM {
  static constexpr u32 HOST_RAM_PER_BH = 1u<<14;   //< this is apparently max size w/o DMA alloc fail?
  //static constexpr u32 HOST_RAM_PER_BH = 1u<<13; //< had been here for a long time though
  static constexpr u32 MIN_GTEED_HOST_RAM_PER_BH = 1u<<14;
}
