#include "BHHD.h"
#include <cassert>
#include <iostream>

namespace MFM {
  BHHD::BHHD()
  {}

  BHHD::~BHHD() {
  }

  void BHHD::enumerate() {
    assert(getDeviceCount()==0);
    std::vector<int> pci_devices = PCIDevice::enumerate_devices();

    assert(!pci_devices.empty());
    for (auto did : pci_devices) {
      mBHDevices.push_back(BHDevice(did));
    }

  }
}
