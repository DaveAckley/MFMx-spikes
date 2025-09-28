// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>
//AHAX #include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/pci_device.hpp"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_soc_descriptor.h"

#include "umd/device/blackhole_implementation.h"

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/host_mem_address_map.h"
#include "blackhole/l1_address_map.h"

#include "Constants.h"
#include "itype.h"
#include "TLBMap.h"

using namespace tt::umd;

namespace MFM {

  int ttmain(std::string rvcode) {
    printf("CODE PAYLOAD %lu,%03lu B\n",rvcode.length()/1000u,rvcode.length()%1000u);
    std::vector<int> pci_devices = PCIDevice::enumerate_devices();
    if (pci_devices.empty()) {
      std::cerr << "No devices found" << std::endl;
      return 1;
    }

    std::cout << "Found " << pci_devices.size() << " device(s)" << std::endl;

    for (int device_id : pci_devices) {
      std::cout << "\n=== Device " << device_id << " (Before Initialization) ===" << std::endl;

      std::unique_ptr<TTDevice> device = TTDevice::create(device_id);

      if (device->get_arch() != tt::ARCH::BLACKHOLE)
        FATAL("NOT BH?");

      {
        TLBMap map;
        map.initMap(device.get());
      }
      std::cout << "GABONG CONINUO" << std::endl;

      uint32_t test_addrSCRATCH = device->get_architecture_implementation()->get_arc_reset_scratch_offset();
      uint32_t test_addrAHAX = device->get_architecture_implementation()->get_tensix_soft_reset_addr();
      uint32_t test_addrHC = RISCV_DEBUG_REG_SOFT_RESET_0;
      uint32_t test_addr = test_addrAHAX;
      uint32_t original_value = device->bar_read32(test_addr);
      std::cout << "SOFT RESET Original value at 0x" << std::hex << test_addr << ": 0x" << original_value << std::dec
                << std::endl;

      std::cout << "Testing device memory operations (without init)..." << std::endl;
      //        uint32_t test_data = 0x12345678;
      uint32_t test_data = SOFT_RESET_ALL_RISCV;
      uint32_t read_data = 0;
      auto test_core = tt_xy_pair(13, 8);
      //        printf("(%lu,%lu) TLB idx = %d\n", test_core.x, test_core.y, map.getTLBIdxIfAny(test_core.x,test_core.y));

      uint64_t mem_addr = 0x0;

      device->write_to_device(rvcode.c_str(), test_core, mem_addr, rvcode.length());
      u8 buf[150000];
      device->read_from_device(buf, test_core, mem_addr, rvcode.length());

      std::cout << "SOFT RESET Device memory operation: wrote " << rvcode.length() << ", read " << rvcode.length() << " MATCH STATUS = " << memcmp(rvcode.c_str(),buf,rvcode.length()) <<std::endl;
        
      if (false) {
        std::cout << "\n=== Now calling init_tt_device() ===" << std::endl;
        device->init_tt_device();

        std::cout << "Clock: " << device->get_clock() << " MHz" << std::endl;
        std::cout << "Board ID: 0x" << std::hex << device->get_board_id() << std::dec << std::endl;
        std::cout << "Temperature: " << device->get_asic_temperature() << "°C" << std::endl;
        std::cout << "ArcMessenger available: " << (device->get_arc_messenger() ? "Yes" : "No") << std::endl;
        std::cout << "ArcTelemetryReader available: " << (device->get_arc_telemetry_reader() ? "Yes" : "No")
                  << std::endl;

        ChipInfo chip_info = device->get_chip_info();
        tt_SocDescriptor soc_desc(
                                  device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
        if (tensix_cores.empty()) {
          std::cout << "No Tensix cores available" << std::endl;
          continue;
        }
        for (u32 i = 0; i < tensix_cores.size(); ++i) {
          std::cout << "TENSIX CORE " << tensix_cores[i].str() << std::endl;
        }

        CoreCoord tensix_core = tensix_cores[0];
        std::cout << tensix_core.str() << std::endl;

        uint32_t init_test_data = 0x87654321;
        uint32_t init_read_data = 0;

        //        uint64_t init_mem_addr = 0x0;
        uint64_t init_mem_addr = RISCV_DEBUG_REG_SOFT_RESET_0;

        device->write_to_device(&init_test_data, tensix_core, init_mem_addr, sizeof(init_test_data));
        device->read_from_device(&init_read_data, tensix_core, init_mem_addr, sizeof(init_read_data));
      
        std::cout << "Post-init memory operation: wrote 0x" << std::hex << init_test_data << ", read 0x"
                  << init_read_data << std::dec << std::endl;
      }
    }

    std::cout << "\nDemo complete" << std::endl;
    return 0;
  }
}
