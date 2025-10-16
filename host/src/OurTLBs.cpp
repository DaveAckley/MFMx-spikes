#include "OurTLBs.h"
#include <cstring>
#include "BaseCar.h"
#include "TransportBlock.h"

// use the source AHAX ?
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include "Constants.h"
#include "t6main-exports.h"

namespace MFM {

  OurTLBs::OurTLBs(int fd)
  : mDevFD(fd)
  , mMapAll(0)
  {
    memset(tlbInfos,0u,sizeof(tlbInfos));
  }

  void OurTLBs::allocateHostRAM(size_t sizePerT6) {
    mT6BufferSize = sizePerT6;
    size_t size = mT6BufferSize * (AHAX_TLBI_L1_LAST_UNI+1u);
    pinned_host_buffer_t* buf = &mPinnedHostBuf;

    if (mT6BufferSize == 0u || buf->size != 0u) HOST_FATAL(ILLEGAL_ARGUMENT,"WHY ARE WE HERE");

    const u32 PAGE_SIZE = 4096;
    buf->size = ((size+PAGE_SIZE-1)/PAGE_SIZE) * PAGE_SIZE;
    printf("Trying to allocate %lu/0x%lx (%lu + %lu) for host RAM buffer\n",
           buf->size, buf->size, mT6BufferSize, buf->size - mT6BufferSize);
    
    // Try doing a regular allocation and pinning it.
    // This should work for 4 KiB allocations, or for any size if an IOMMU is present and enabled.
    void* memory = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("(REGULAR) mmap returned %p\n",memory);
    {
      struct tenstorrent_pin_pages_extended pin_req;
      memset(&pin_req, 0, sizeof(pin_req));
      printf("BEF pin req in.os = %u, in.flags = %u\n",pin_req.in.output_size_bytes,pin_req.in.flags);
      pin_req.in.output_size_bytes = sizeof(pin_req.out);
      pin_req.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN;
      pin_req.in.size = buf->size;
      printf("AFT pin req in.os = %u, in.flags = %u\n",pin_req.in.output_size_bytes,pin_req.in.flags);
      if (memory != MAP_FAILED) {
        pin_req.in.virtual_address = (uint64_t)(uintptr_t)memory;
        printf("(REGULAR) IOCTL on %u\n",mDevFD);
        if (ioctl(mDevFD, TENSTORRENT_IOCTL_PIN_PAGES, &pin_req) >= 0) {
          buf->host_ptr = memory;
          buf->noc_addr = pin_req.out.noc_address;
          return;
        }
        printf("(REGULAR) IOCTL failed: %s\n",strerror(errno));
        munmap(memory, buf->size);
      }
      // Try doing a huge page allocation and pinning it.
      // This should work for any size configured as a huge page size.
      memory = mmap(NULL, buf->size,
                    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
                    MAP_HUGETLB | (__builtin_ctzll(buf->size) << MAP_HUGE_SHIFT), -1, 0);
      if (memory == MAP_FAILED) 
        printf("(HUGE) mmap failed: %s\n",strerror(errno));
      else {
        pin_req.in.virtual_address = (uint64_t)(uintptr_t)memory;
        printf("(HUGE) IOCTL on %u\n",mDevFD);
        if (ioctl(mDevFD, TENSTORRENT_IOCTL_PIN_PAGES, &pin_req) >= 0) {
          buf->host_ptr = memory;
          buf->noc_addr = pin_req.out.noc_address;
          return;
        }
        printf("(HUGE) IOCTL failed: %s\n",strerror(errno));
        munmap(memory, buf->size);
      }
    }

    printf("(DMA) Trying DMA allocation\n");
    // Try doing a DMA allocation.
    // This should work for any size up to the DMA buffer size limit, subject to host memory fragmentation.
    {
      struct tenstorrent_allocate_dma_buf dma_req;
      memset(&dma_req, 0, sizeof(dma_req));
      dma_req.in.requested_size = buf->size;
      dma_req.in.flags = TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA;
      printf("(DMA) IOCTL on %u\n",mDevFD);
      if (ioctl(mDevFD, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dma_req) >= 0) {
        printf("(DMA) MMAP for %lu\n",buf->size);
        memory = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, mDevFD, dma_req.out.mapping_offset);
        if (memory != MAP_FAILED) {
          buf->host_ptr = memory;
          buf->noc_addr = dma_req.out.noc_address;
          printf("(DMA) MMAP got hp %p..%p\n",(char*)memory,((char*)memory)+buf->size);
          return;
        }
        printf("(DMA) MMAP failed: %s\n",strerror(errno));
      } else
        printf("(DMA) IOCTL failed: %s\n",strerror(errno));
    }

    HOST_FATAL(UNSUPPORTED_OPERATION,"PIN FAILED");
  }

  LogCarStorage::LogCar * OurTLBs::getLogCarHost(u32 tlbi, u32 carnum) {
    MFM_API_ASSERT_NONNULL(mMapAll);
    char * tlbBase = (((char*) mMapAll) + tlbi*AHAX_CONSTANT2M);
    u32 logcarsize = sizeof(MFM::LogCarStorage::LogCar);
    return (LogCarStorage::LogCar*) (tlbBase+carnum*logcarsize);
  }

  u32 OurTLBs::getLogCarT6(u32 tlbi, u32 carnum) {
    MFM_API_ASSERT_NONNULL(mMapAll);
    u32 base = MFM::T6::transportblock_log_start;
    u32 logcarsize = sizeof(MFM::LogCarStorage::LogCar);
    return base+carnum*logcarsize;
  }
    
  void OurTLBs::allocateTLBs() {

    // allocate our 2MB tlbs
    for (unsigned i = 0; i < AHAX_TLB2M_COUNT; ++i) {

      struct tenstorrent_allocate_tlb tlbio;
      tlbio.in.size = AHAX_CONSTANT2M;
      ASSERT(ioctl(mDevFD, TENSTORRENT_IOCTL_ALLOCATE_TLB, &tlbio) >= 0);
      tlbInfos[i].mAllocOut = tlbio.out;

      if (i < 2u || i >= AHAX_TLB2M_COUNT-2u)
        printf("TLBALLOC id=%u uc=0x%lx wc=0x%lx\n",
               tlbInfos[i].mAllocOut.id,
               tlbInfos[i].mAllocOut.mmap_offset_uc,
               tlbInfos[i].mAllocOut.mmap_offset_wc);
    }
  }

  void OurTLBs::configureTLBs() {
    // get addrs to cover all our needs
    mMapAll = mmap(NULL, AHAX_MMAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT(mMapAll != MAP_FAILED);
    printf("MMAP AT %p\n", mMapAll);

    // configure and map all but the last two to the fleet - L1 uni
    for (unsigned i = AHAX_TLBI_L1_FIRST_UNI; i <= AHAX_TLBI_L1_LAST_UNI; ++i) {
      U16C nocc = U16C::makeNocCoordFromTLBI(i);

      // configure as T6/L1 access window
      void * ptr = configureL1Window(i,{nocc,nocc});

      if (i < 2u || i >= AHAX_TLB2M_COUNT-4u)
        printf("TLBCFG %u @ (%u,%u) mapped at %p\n",i,nocc.x,nocc.y,ptr);
      
    }
  
    // map last two
    {
      void * ptr = configureMulticastWindow(AHAX_TLBI_L1_MULTI, 0x0, /*wc=*/ true);
      printf("L1MULTI %u mapped at %p\n",AHAX_TLBI_L1_MULTI,ptr);
    }
    {
      void * ptr = configureMulticastWindow(AHAX_TLBI_DEBUG_MULTI, 0xFFB12000, false);
      printf("DEBUGMULTI %u mapped at %p\n",AHAX_TLBI_DEBUG_MULTI,ptr);
    }
  }

  void OurTLBs::writeToWords(u32 tlbi, u32 destAddr, u32 * words, u32 wordCount) {
    u32 * tlbBase = (u32*) ((char*) mMapAll + tlbi*AHAX_CONSTANT2M);
    u32 * destTop = (u32*) (((u64) destAddr)&~((u64) AHAX_CONSTANT2M_MASK));
    u32 destOffsetWords = (destAddr&AHAX_CONSTANT2M_MASK)>>2u;
    if (destTop != (u32*) (((u64) tlbInfos[tlbi].mRemoteBaseAddress)&~((u64) AHAX_CONSTANT2M_MASK)))
      HOST_FATAL(BAD_ALIGNMENT,
                 "TOP BIT MISMATCH FOR TLBI %d base %p top %p (from 0x%x, wanted 0x%x)\n",
                 tlbi,tlbBase,destTop,destAddr,destOffsetWords<<2u);
    for (u32 i = 0u; i < wordCount; ++i) {
      if (i < 10u || i >= wordCount - 10u)
        printf("WT %d/%d %p/0x%08x = 0x%08x\n",
               tlbi,i,
               (volatile uint32_t*)(tlbBase + destOffsetWords + i),
               destAddr+4u*i,
               words[i]);
      *(volatile uint32_t*)(tlbBase + destOffsetWords + i) = words[i];
    }
  }

  void OurTLBs::readFromWords(u32 tlbi, u32 srcByteAddr, u32 * words, u32 wordCount) {
    char * tlbBase = ((char*) mMapAll + tlbi*AHAX_CONSTANT2M);
    u32 srcTop = srcByteAddr&~AHAX_CONSTANT2M_MASK;
    u32 srcOffsetWords = srcByteAddr&AHAX_CONSTANT2M_MASK;
    if (srcTop != 0u)
      printf("IGNORING TOP BITS FOR TLBI %d base %p top 0x%x (from 0x%x, using 0x%x)\n",
             tlbi,tlbBase,srcTop,srcByteAddr,srcOffsetWords<<2u);
    for (u32 i = 0u; i < wordCount; ++i) {
      words[i] = *(volatile uint32_t*)(tlbBase + srcOffsetWords + (i<<2u));
      if (false) {
        if (i < 20u || i >= wordCount - 20u)
          printf("RF %d %p = 0x%08x (%d)\n",
                 i,(volatile uint32_t*)(tlbBase + srcOffsetWords + (i<<2u)),words[i],tlbi);
      }
    }
  }
  
  void * OurTLBs::configureMulticastWindow(u32 tlbi, u32 address, bool wc) {
    return configureWindow(tlbi, {{1,2},{16,11}}, address, wc);
  }

  void * OurTLBs::configureL1Window(u32 tlbi, U16CRange range) {
    return configureWindow(tlbi, range, 0u, true);
  }

  void * OurTLBs::configureWindow(u32 tlbi, U16CRange range, u32 address, bool wc) {
    unsigned ismulti = range.area() > 1u;
    //printf("CWD %d, %d (%d,%d) (%d,%d)\n",tlbi,ismulti,range.end.x,range.end.y,range.start.x,range.start.y);
    struct tenstorrent_configure_tlb confio;
    memset(&confio,0,sizeof(confio));
    struct tenstorrent_configure_tlb_in & cfin = confio.in;
    struct tenstorrent_configure_tlb_out & cfout = confio.out;

    cfin.id = tlbInfos[tlbi].mAllocOut.id;
    tlbInfos[tlbi].mRemoteBaseAddress = address;
    struct tenstorrent_noc_tlb_config & cfnoc = cfin.config;
    cfnoc.addr = address&~AHAX_CONSTANT2M_MASK; // window starting address in (x,y) space?
    cfnoc.x_end = range.end.x;
    cfnoc.y_end = range.end.y;
    cfnoc.x_start = range.start.x;     // need start and end for unicast?
    cfnoc.y_start = range.start.y;
    cfnoc.noc = 0u;
    cfnoc.mcast = ismulti;
    // cfnoc.ordering = 0u; // ??? 1 == strict? 0 == relaxed?
    // cfnoc.linked
    // cfnoc.static_vc
    
    ASSERT(ioctl(mDevFD, TENSTORRENT_IOCTL_CONFIGURE_TLB, &confio) >= 0);

    // map window into our space
    u64 offset = wc ?
      tlbInfos[tlbi].mAllocOut.mmap_offset_wc :
      tlbInfos[tlbi].mAllocOut.mmap_offset_uc;
    void * ptr;
    if ((ptr = mmap((void*)(((char*) mMapAll) + tlbi*AHAX_CONSTANT2M), AHAX_CONSTANT2M,
                    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
                    mDevFD, offset)) == MAP_FAILED)
      HOST_FATAL(OPERATION_FAILED,"MMAP FAIL");

    return ptr;
  }

  bool OurTLBs::updateLogTransports() {
    pinned_host_buffer_t& buf = mPinnedHostBuf;
    void * hostmem = buf.host_ptr;
    u64 hostnocaddr = buf.noc_addr;

    for (unsigned i = AHAX_TLBI_L1_FIRST_UNI; i <= AHAX_TLBI_L1_LAST_UNI; ++i) {
      u32 t6offset = i * mT6BufferSize;
      void * pert6addr = ((char*)hostmem) + t6offset;
      LogCarStorage & stg = *(LogCarStorage*) pert6addr;
      for (u32 car = 0u; car < LogCarStorage::CAR_COUNT; ++car) {
        LogCarStorage::LogCar & lc = stg.mLogCars[car];
        CarSig cs = lc.getHeader();
        u32 caroffset = ((char*)&lc) - ((char*) &stg);
        if (false/*i < 2 || i > 137*/) {
          printf("ULOG %u %u 0x%02x cls %p lc %p noc 0x%lx + %lu\n",
                 i,car,
                 (u32) cs.mCarMagic,
                 &stg,
                 &lc,
                 hostnocaddr + t6offset + caroffset,
                 sizeof(lc)
                 );
        }
        if (lc.isComplete() && lc.getCarState() == CarState::HEADING_INBOUND) {
          LogBlock & lb = lc.getContent();
          printf("LOG:%u.%u<",i,car);
          for (u32 i = 0u; i < lb.mLength; ++i) 
            printf("%c",lb.mData[i]);
          printf(">%u.%u:\n",i,car);
          lb.reset();           // empty car
          lc.setCarState(CarState::HEADING_OUTBOUND);
        }
      }
    }
    sleep(.00001);
    return true;
  }

}
