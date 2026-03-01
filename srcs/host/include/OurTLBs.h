#pragma once          /* -*- mode: C++ -*- */

#include "utils.h"
#include "HostUtils.h"

#include "FATAL.h"
#include "U16C.h"
#include "TTKMDStuff.h"
#include "TransportBlock.h"
#include "T6Image.h"
#include "HostCommsMap.h"
#include "CommsModule.h"

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

    // ACCESS REAL HOST MEMORY (FOR INCOMING FROM T6s)
    LogCarStorage & getLogCarStorageHost(u32 tlbi) const ;
    LogCarStorage::LogCar & getLogCarHost(u32 tlbi, u32 carnum) const ;

    // ACCESS FAKE MMAP'D ADDRS (TO R/W REMOTE T6s)
    char * getL1HostAddressForTLBI(u32 tlbi) ; //< host-mapped address of L1 addr 0 for T6 tlbi

    u32 getLogCarT6L1(u32 tlbi, u32 carnum) ;

    bool updateTransports(bool includeEWs) ;
    void updateLogCars(unsigned tlbi) ;
    void updateEWCars(unsigned tlbi) ;

    void setDeviceInfo(u32 cardNum, s32 devfd) {
      mDevCardNum = cardNum;
      mDevFD = devfd;
      mHostCommsMap.init("OurBH#"+std::to_string(mDevCardNum));
    }

    const HostCommsMap & getHostCommsMap() const { return mHostCommsMap; }

    void addCommsModule(CommsModule & cm) {
      mHostCommsMap.addCommsModule(cm);
    }

    void setHostMemoryBaseAddress() {
      void * base = hostRAMPtr();
      MFM_API_ASSERT_NONNULL(base);
      mHostCommsMap.setHostMemoryBaseAddress(base);
    }
    
    void allocateHostRAM(size_t bufferSize) ;  // also setHostMemoryBaseAddress
    void doHostRAMAllocation(size_t sizePerT6) ;

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

    u32 hostRAMSizePerT6() const { return mT6HostBufferSize; }

    void * hostRAMPtrForTLBI(u32 tlbi) const {
      char * all = (char*) hostRAMPtr();
      if (!all || tlbi > AHAX_TLBI_L1_LAST_UNI) return 0;
      return (void*) (all + tlbi * hostRAMSizePerT6());
    }

    struct TLBInfo {
      struct tenstorrent_allocate_tlb_out mAllocOut;
      const T6Image * mDeployedImage;
      u32 mT6GridStart;         //< if image has a T6Grid block
      u32 mLogTransportBlockStart;
      //u32 mLogCars;
      u32 mEWTransportBlockStart;
      u32 mRemoteBaseAddress;
      u8 mFailStatus;
      u8 mNextLogCarIndex;
      u32 mLastWatchdog[5];
      bool mStuckDog[5];

      void setDeployedImage(const T6Image & img) ;

      const T6Image * getDeployedImageIfAny() const {
        return mDeployedImage;
      }
      u32 getLogCarIndex() const { return mNextLogCarIndex; }
      u32 advanceLogCarIndex() {
        if (++mNextLogCarIndex >= LogCarStorage::CAR_COUNT)
          mNextLogCarIndex = 0u;
        return mNextLogCarIndex;
      }
    };
    TLBInfo & getTLBInfo(u32 tlbi) ;

    bool configureT6ImageForHostComms(T6Image & t6i) {
      return mHostCommsMap.configureT6ImageForHostComms(t6i);
    }
    
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
    HostCommsMap mHostCommsMap;
    void * mMapAllT6L1;         //< start of ~140*2M addrs for host to R/W T6 L1
    size_t mT6HostBufferSize;   //< size of ~140*8K pinned host RAM for T6s to (R/)W 
    bool mDMABufferPretendDeleted;
    u64 mEWsShipped, mEWsReturned, mEWsCommitted;
  };
} // namespace MFM

