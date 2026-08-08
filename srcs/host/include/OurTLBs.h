#pragma once          /* -*- mode: C++ -*- */

#include "utils.h"
#include "HostUtils.h"

#include "FATAL.h"
#include "UxC.h" // for U16C
#include "TTKMDStuff.h"
#include "TC.h"
#include "T6Image.h"
#include "HostCommsMap.h"
#include "CommsModule.h"
#include "P4Atom.h"
#include "PT_LogBlock.h" // for LogBlockStg and frens
#include "PT_ACacheBlock.h" // ditto ACacheBlockStg
#include "ZHostDecompressor.h"

#include <pybind11/functional.h> // for std::function?

namespace MFM {

  class Blackhole; // FORWARD
  
  class OurTLBs {
  public:

    // constants
    static const u32 AHAX_CONSTANT2M = (1u<<21);
    static const u32 AHAX_CONSTANT2M_MASK = AHAX_CONSTANT2M-1u;
    static const u32 AHAX_TLBI_L1_FIRST_UNI = 0;
    static const u32 AHAX_TLBI_L1_LAST_UNI = 139;
    static const u32 AHAX_TLBI_L1_MULTI = (AHAX_TLBI_L1_LAST_UNI + 1);
    static const u32 AHAX_TLBI_DEBUG_MULTI = (AHAX_TLBI_L1_MULTI + 1);
    static const u32 AHAX_TLBI_DEBUG_UNI = (AHAX_TLBI_DEBUG_MULTI + 1);

    OurTLBs(Blackhole & bh) ;

    // ACCESS REAL HOST MEMORY (FOR INCOMING FROM T6s)
    LogBlockStg & getLogBlockStgHost(u32 tlbi) const ;
    LogBlockStg::CAR_TYPE & getLogBlockHost(u32 tlbi, u32 carnum) const ;

    ACacheBlockStg & getACacheBlockStgHost(u32 tlbi) const ;
    ACacheBlockStg::CAR_TYPE & getACacheBlockHost(u32 tlbi, u32 carnum) const ;

    // ACCESS FAKE MMAP'D ADDRS (TO R/W REMOTE T6s)
    char * getL1HostAddressForTLBI(u32 tlbi) ; //< host-mapped address of L1 addr 0 for T6 tlbi

    //    u32 getLogCarT6L1(u32 tlbi, u32 carnum) ;

    bool updateTransports(bool includeEWs) ;
    void updateLogBlocks(unsigned tlbi) ;

    void updateACacheBlocks(unsigned tlbi) ;
    void applyACacheBlock(ACacheBlockPayload& acbp, u32 tlbi) ;
    void updateEWCars(unsigned tlbi) ;

    void setDeviceInfo(u32 chipNum, s32 devfd) {
      mDevChipNum = chipNum;
      mDevFD = devfd;
      mHostCommsMap.init("OurBH#"+std::to_string(mDevChipNum));
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
    
#if 0
    // ---- One-time setup --------------------------------------------------------
    //
    // Programs AHAX_TLBI_DEBUG_UNI with:
    //   local_offset = 0xFFB00000  (2 MiB-aligned base covering debug regs + PC snapshots)
    //   noc_sel      = 0           (NoC0)
    //   mcast        = 0           (unicast)
    //   ordering     = 0           (Default)
    //   x_end, y_end = 0           (call retarget before reading)
    //
    // After init, the window maps [0xFFB0_0000, 0xFFB1_FFFF] in the target tile.
    // To read debug regs:  window_base + DEBUG_REGS_WINDOW_OFFSET  (+ reg offset)
    // To read PC snapshots: window_base + HART_PC_OFFSETS[i]

    inline volatile u8* t6_debug_tlb_init(volatile void*  bar0_base,
                                          T6DebugTlb*     state) {
      // 2 MiB-aligned local_offset: round RISCV_DEBUG_REGS_START_ADDR down to 2 MiB.
      // 0xFFB12000 & ~0x1FFFFF = 0xFFB00000
      const uint64_t local_offset =
        (RISCV_DEBUG_REGS_START_ADDR & ((1ULL << 43) - 1)) & ~(TLB_WINDOW_2MIB - 1);

      // Assemble static 96-bit config (x_end = y_end = 0).
      const uint64_t val96 =
        (local_offset << 0);

      state->static_low  = static_cast<uint32_t>(val96 & 0xFFFFFFFF);
      state->static_mid  = static_cast<uint32_t>((val96 >> 32) & 0xFFFFFFFF);
      state->static_high = 0; // val96 never exceeds 43 bits so high32 always 0

      // Point to config registers for our TLB index.
      volatile u8* cfg_base =
        reinterpret_cast<volatile u8*>(bar0_base) + BAR0_TLB_CONFIG_OFFSET;
      state->cfg_low  = reinterpret_cast<volatile uint32_t*>(
                                                             cfg_base + AHAX_TLBI_DEBUG_UNI * TLB_CONFIG_ENTRY_SIZE);
      state->cfg_mid  = state->cfg_low + 1;
      state->cfg_high = state->cfg_low + 2;

      // Write static config.
      *state->cfg_low  = state->static_low;
      *state->cfg_mid  = state->static_mid;
      *state->cfg_high = state->static_high;

      // Record window base for reads.
      state->window_base =
        reinterpret_cast<volatile u8*>(bar0_base) +
        (uint64_t)AHAX_TLBI_DEBUG_UNI * TLB_WINDOW_2MIB;

      return state->window_base;
    }
#endif

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

    //    bool writeP4Atom(U16C gridAddress,const P4Atom atom) ;

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
    void debugReadWords(U8C fromNoC0,u32 srcByteAddr, u32 * words, u32 wordCount) ;
    void readPCSnapshots(U8C fromNoC0, u32 words[5]) ;

    using StackBlock = std::deque<u32>;
    u32 readHartStackAfterFail(U8C fromNoC0, u8 hartnum, StackBlock & sb, u32 atfailsp, u32 * optBaseAddrPtr) ;

    bool hasHostRAM() const { return mPinnedHostBuf.host_ptr != 0; }
    size_t hostRAMSize() const { return mPinnedHostBuf.size; }
    void * hostRAMPtr() const { return mPinnedHostBuf.host_ptr; }
    u64 hostRAMNoCAddr() const { return mPinnedHostBuf.noc_addr; }

    u32 hostRAMSizePerT6() const { return mT6HostBufferSize; }

    void * hostRAMPtrForTLBI(u32 tlbi) const {
      char * all = (char*) hostRAMPtr();
      if (!all || tlbi > AHAX_TLBI_L1_LAST_UNI) return 0;
      return (void*) (all + tlbi * hostRAMSizePerT6());
    }

    struct membuf : std::streambuf {
      membuf(char* begin, u32 len) { this->setg(begin, begin, begin + len);}
    };



    struct TLBInfo {
      struct tenstorrent_allocate_tlb_out mAllocOut;
      const T6Image * mDeployedImage;
      u32 mT6GridStart;         //< if image has a T6Grid block
      u32 mACacheTransportBlockStart;
      u32 mLogTransportBlockStart;
      u32 mEWTransportBlockStart;
      u32 mRemoteBaseAddress;
      u8 mFailStatus;
      u8 mNextLogBlockIndex;
      u8 mNextACacheBlockIndex;
      u32 mLastWatchdog[5];
      bool mStuckDog[5];
      bool mHasBeenDumped;

      u32 mACBPacketsReceived;

      ZHostDecompressor mZHD; // everybody gets one, at least for now..

      void setDeployedImage(const T6Image & img) ;

      const T6Image * getDeployedImageIfAny() const {
        return mDeployedImage;
      }

      const T6Image & getDeployedImageOrDie() const {
        MFM_API_ASSERT_NONNULL(mDeployedImage);
        return *mDeployedImage;
      }

      u32 getLogBlockIndex() const { return mNextLogBlockIndex; }
      u32 advanceLogBlockIndex() {
        if (++mNextLogBlockIndex >= LogBlockStg::CAR_COUNT)
          mNextLogBlockIndex = 0u;
        return mNextLogBlockIndex;
      }

      u32 getACacheBlockIndex() const { return mNextACacheBlockIndex; }
      u32 advanceACacheBlockIndex() {
        if (++mNextACacheBlockIndex >= ACacheBlockStg::CAR_COUNT)
          mNextACacheBlockIndex = 0u;
        return mNextACacheBlockIndex;
      }
    };

    TLBInfo & getTLBInfo(u32 tlbi) ;

    bool configureT6ImageForHostComms(T6Image & t6i) {
      return mHostCommsMap.configureT6ImageForHostComms(t6i);
    }
    
  private:
    static const u32 AHAX_TLB2M_COUNT = (AHAX_TLBI_DEBUG_UNI + 1);
    static const u32 AHAX_MMAP_SIZE = (AHAX_CONSTANT2M * AHAX_TLB2M_COUNT);

    // methods
    void * configureMulticastWindow(unsigned tlbi, u32 address, bool wc) ;
    void * configureL1Window(unsigned tlbi, U16CRange range) ;
    void * configureWindow(unsigned tlbi, U16CRange range, u32 address, bool wc) ;
    void * configureDebugWindow(unsigned tlbi) ;
    void * configureDebugNoCOnly(U8C forNoC0) ;

    // data
    TLBInfo mTLBInfos[AHAX_TLB2M_COUNT];
    pinned_host_buffer_t mPinnedHostBuf; 
    u32 mDevChipNum;
    s32 mDevFD;
    Blackhole * mBlackholePtr;
    HostCommsMap mHostCommsMap;
    void * mMapAllT6;         //< start of ~140*2M addrs for host to R/W T6 (usually L1)
    size_t mT6HostBufferSize;   //< size of ~140*8K pinned host RAM for T6s to (R/)W 
    bool mDMABufferPretendDeleted;
    u64 mEWsShipped, mEWsReturned, mEWsCommitted;
  };
} // namespace MFM

