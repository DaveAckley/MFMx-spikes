#ifndef BHHD_H          /* -*- mode: C++ -*- */
#define BHHD_H

#include "Util.h"
#include "Constants.h"
#include "HostBlock.h"
#include "RandMT.h"
#include "BHDevice.h"

namespace MFM {
 
  /** Blackhole Host Daemon ('user mode driver'), intermediate AHAX
      style, controlling access to all BHDevices in a system.
   */
  class BHHD {
  public:
    BHHD() ;
    ~BHHD() ;

    void enumerate() ;

    u32 getDeviceCount() const { return mBHDevices.size(); }
    const BHDevice & getBHDevice(u32 devnum) const { return mBHDevices[devnum]; }
    BHDevice & getBHDevice(u32 devnum) { return mBHDevices[devnum]; }

    using iterator = std::vector<BHDevice>::iterator;
    using const_iterator = std::vector<BHDevice>::const_iterator;

    iterator begin() { return mBHDevices.begin(); }
    iterator end() { return mBHDevices.end(); }

    const_iterator cbegin() const { return mBHDevices.cbegin(); }
    const_iterator cend() const { return mBHDevices.cend(); }
    
  private:
    std::vector<BHDevice> mBHDevices;
  };

} // namespace MFM

#endif /* BHHD_H */
