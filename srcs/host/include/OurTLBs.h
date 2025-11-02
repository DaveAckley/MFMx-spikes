#pragma once          /* -*- mode: C++ -*- */

#include "utils.h"
#include "HostUtils.h"

#include "FATAL.h"
#include "U16C.h"
#include "TTKMDStuff.h"
#include "TransportBlock.h"
#include "BHTag.h"

#include <pybind11/functional.h> // for std::function?

namespace MFM {

  class OurTLBs {
  public:

    // constants
    static const u32 AHAX_CONSTANT2M = (1u<<21);
    static const u32 AHAX_CONSTANT2M_MASK = AHAX_CONSTANT2M-1u;
    static const u32 AHAX_TLBI_L1_FIRST_UNI = 0;
    static const u32 AHAX_TLBI_L1_LAST_UNI = 139;
    static const u32 AHAX_TLBI_L1_MULTI = (AHAX_TLBI_L1_LAST_UNI + 1);
    static const u32 AHAX_TLBI_DEBUG_MULTI = (AHAX_TLBI_L1_MULTI + 1);

    OurTLBs() ;
    LogCarStorage::LogCar * getLogCarHost(u32 tlbi, u32 carnum) ;
    bool updateTransports() ;
    void updateLogCars(unsigned tlbi) ;
    void updateEWCars(unsigned tlbi) ;
    u32 getLogCarT6(u32 tlbi, u32 carnum) ;

    void setDeviceInfo(u32 cardNum, s32 devfd) {
      mDevCardNum = cardNum;
      mDevFD = devfd;
    }

    void allocateHostRAM(size_t bufferSize) ;
    void deallocateHostRAM() ;
    void stopPretendingHostRAMisDeallocated() ;

    void allocateTLBs() ;
    void deallocateTLBs() ;

    void configureTLBs() ;
    void unconfigureTLBs() ;

    void resetTheFleet() ;

    //////
    void write32(u32 tlbi, u32 addr, u32 data) {
      writeToWords(tlbi, addr, &data, 1u);
    }
    void writeToBytes(u32 tlbi, u32 destByteAddr, u8 * bytes, u32 byteCount) {
      if (((((uintptr_t) bytes) & 0x3) != 0u) ||
          (byteCount & 0x3) != 0u)
        HOST_FATAL(BAD_ALIGNMENT,"BAD WRITE ALIGNMENT\n");
      writeToWords(tlbi, destByteAddr, (u32 *) bytes, byteCount/4u);
    }
    void writeToWords(u32 tlbi, u32 destByteAddr, u32 * words, u32 wordCount) ;

    u32 read32(u32 tlbi, u32 addr) {
      u32 ret;
      readFromWords(tlbi, addr, &ret, 1u);
      return ret;
    }
    void readFromBytes(u32 tlbi, u32 srcByteAddr, u8 * bytes, u32 byteCount) {
      if (((((uintptr_t) bytes) & 0x3) != 0u) ||
          (byteCount & 0x3) != 0u)
        HOST_FATAL(BAD_ALIGNMENT,"BAD READ ALIGNMENT\n");
      readFromWords(tlbi, srcByteAddr, (u32 *) bytes, byteCount/4u);
    }

    void readFromWords(u32 tlbi, u32 srcByteAddr, u32 * words, u32 wordCount) ;

    bool hasHostRAM() const { return mPinnedHostBuf.host_ptr != 0; }
    size_t hostRAMSize() const { return mPinnedHostBuf.size; }
    void * hostRAMPtr() const { return mPinnedHostBuf.host_ptr; }
    u64 hostRAMNocAddr() const { return mPinnedHostBuf.noc_addr; }

    struct TLBInfo {
      struct tenstorrent_allocate_tlb_out mAllocOut;
      u32 mRemoteBaseAddress;
      u8 mFailStatus;
    };
    TLBInfo & getTLBInfo(u32 tlbi) ;

  private:
    static const u32 AHAX_TLB2M_COUNT = (AHAX_TLBI_DEBUG_MULTI + 1);
    static const u32 AHAX_MMAP_SIZE = (AHAX_CONSTANT2M * AHAX_TLB2M_COUNT);

    // methods
    void * configureMulticastWindow(unsigned tlbi, u32 address, bool wc) ;
    void * configureL1Window(unsigned tlbi, U16CRange range) ;
    void * configureWindow(unsigned tlbi, U16CRange range, u32 address, bool wc) ;

    // data
    TLBInfo mTLBInfos[AHAX_TLB2M_COUNT];
    pinned_host_buffer_t mPinnedHostBuf;
    u32 mDevCardNum;
    s32 mDevFD;
    void * mMapAll;
    size_t mT6BufferSize;
    bool mDMABufferPretendDeleted;
    u64 mEWsShipped, mEWsReturned, mEWsCommitted;
  };
} // namespace MFM

