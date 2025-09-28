#include "BHUMD.h"

namespace MFM {

  BHUMD::BHUMD(u32 deviceNumber)
    : mDevNum(deviceNumber)
    , mDevOpen(false)
  {
    /* empty */
  }

  BHUMD::~BHUMD()
  {
    if (mDevOpen)
      close();
  }
  
  s32 BHUMD::open() {
    if (mDevOpen)
      FATAL("ALREADY OPEN");

    if (open_bh_pcie_device())
      FATAL("FAILED");

    mDevOpen = true;

    if (configure_tlb())
      FATAL("FAILED");

    if (deployRISCVCode())
      FATAL("FAILED");

    return 0;
  }

  void BHUMD::tlb_write_u32(uint64_t addr, uint32_t value) {
    *(volatile u32*)set_tlb_addr(addr) = value;
  }

  u32 BHUMD::tlb_read_u32(uint64_t addr) {
    return *(volatile u32*)set_tlb_addr(addr);
  }

  static const uint32_t rv_code[] = {
    // b_setup: # B must be first, as RISCV B has fixed reset address of zero
    0x00000413,             // li s0, 0
    0x0200006f,             // j done_setup
    // t0_setup:
    0x00100413,             // li s0, 1
    0x0180006f,             // j done_setup
    // t1_setup:
    0x00200413,             // li s0, 2
    0x0100006f,             // j done_setup
    // t2_setup:
    0x00300413,             // li s0, 3
    0x0080006f,             // j done_setup
    // nc_setup:
    0x00400413,             // li s0, 4
    // done_setup:
    0x00000917, 0x07090913, // la s2, fn_arguments
    0x00092383,             // lw t2, 0(s2) # function pointer
    0x00492303,             // lw t1, 4(s2) # loop count
    0x21244433,             // sh2add s0, s0, s2
    0x00842283,             // lw t0, 8(s0) # memory pointer
    0x00100493,             // li s1, 1 # constant one (for incrementing)
    0x7c046073,             // csrrsi x0, 8, 0x7c0 # disable L0 cache
    0xc0002e73,             // csrrs t3, x0, 0xc00 # sample the cycle counter
    0x00038067,             // jr t2
    // atomic_loop:
    0x0092a02f,             //   amoadd.w x0, 0(t0), s1
    0xfff30313,             //   subi t1, t1, 1
    0xfe031ce3,             //   bne t1, x0, atomic_loop
    0x0000000f,             // fence
    0xc0002ef3,             // csrrs t4, x0, 0xc00 # sample the cycle counter
    0x0200006f,             // j fini
    // non_atomic_loop:
    0x0002a383,             //   lw t2, 0(t0)
    0x009383b3,             //   add t2, t2, s1
    0x0072a023,             //   sw t2, 0(t0)
    0xfff30313,             //   subi t1, t1, 1
    0xfe0318e3,             //   bne t1, x0, non_atomic_loop
    0x0000000f,             // fence
    0xc0002ef3,             // csrrs t4, x0, 0xc00 # sample the cycle counter
    // fini:
    0x7c047073,             // csrrci x0, 8, 0x7c0 # enable L0 again cache
    0x41ce8eb3,             // sub t4, t4, t3 # compute elapsed cycles
    0x01d42423,             // sw t4, 8(s0)
    0x0000000f,             // fence
    // done:
    0x0000006f              // j done
    // fn_arguments:
  };
#define label_b_setup 0x0
#define label_t0_setup 0x8
#define label_t1_setup 0x10
#define label_t2_setup 0x18
#define label_nc_setup 0x20
#define label_done_setup 0x24
#define label_atomic_loop 0x4c
#define label_non_atomic_loop 0x64
#define label_fini 0x80
#define label_done 0x90
#define label_fn_arguments 0x94

  typedef struct rv_code_arguments_t {
    uint32_t fptr;
    uint32_t loop_count;
    uint32_t per_rv[5];
  } rv_code_arguments_t;

  s32 BHUMD::doTests() {
    // The actually interesting loops.
    uint32_t mem_ptr_base = (sizeof(rv_code) + sizeof(rv_code_arguments_t) + 1023) &~ 1023u;
    uint32_t loop_count = 15000;
    for (unsigned atomic = 0; atomic < 2; ++atomic) {
      printf("FOR DEVICE %d\n",mDevNum);
      if (atomic) {
        printf("\n|------------------- Results for atomic memory accesses -------------------|\n");
      } else {
        printf("|----------------- Results for non-atomic memory accesses -----------------|\n");
      }
      printf("|Memory|RISCV B    |RISCV T0   |RISCV T1   |RISCV T2   |RISCV NC   |Final  |\n");
      printf("|Stride|Cycles/iter|Cycles/iter|Cycles/iter|Cycles/iter|Cycles/iter|Counter|\n");
      printf("|------|-----------|-----------|-----------|-----------|-----------|-------|\n");
      for (uint32_t step_log2 = 0; step_log2 < 10; ++step_log2) {
        uint32_t mem_step = step_log2 ? 2 << step_log2 : 0;
        // Prepare arguments for this iteration of the loops. (They go just after the code)
        volatile rv_code_arguments_t* args = (volatile rv_code_arguments_t*)set_tlb_addr(sizeof(rv_code));
        args->loop_count = loop_count;
        args->fptr = atomic ? label_atomic_loop : label_non_atomic_loop;
        for (unsigned rv = 0; rv < 5; ++rv) {
          uint32_t counter_addr = mem_ptr_base + mem_step * rv;
          args->per_rv[rv] = counter_addr;
          tlb_write_u32(counter_addr, 0); // Initialise counter to zero.
        }
        // Start the RISCVs.
        tlb_write_u32(RISCV_DEBUG_REG_SOFT_RESET_0, 0);
        // Wait for all the RISCVs to finish.
        for (unsigned rv = 0; rv < 5; ++rv) {
          do {} while (tlb_read_u32(0xFFB13138 + rv * 4) < label_done);
        }
        // We're done; put the RISCVs back into reset.
        tlb_write_u32(RISCV_DEBUG_REG_SOFT_RESET_0, SOFT_RESET_ALL_RISCV);
        // Collect the results...
        uint32_t elapsed[5];
        uint32_t final_ctr = 0;
        args = (volatile rv_code_arguments_t*)set_tlb_addr(sizeof(rv_code));
        for (unsigned rv = 0; rv < 5; ++rv) {
          uint32_t counter_addr = mem_ptr_base + mem_step * rv;
          elapsed[rv] = args->per_rv[rv];
          if (rv == 0 || mem_step != 0) {
            final_ctr += tlb_read_u32(counter_addr);
          }
        }
        // ... and format them.
        printf("|%5u |", mem_step);
        for (unsigned rv = 0; rv < 5; ++rv) {
          printf("%10.3f |", (float)elapsed[rv] / (float)loop_count);
        }
        printf("%6u |\n", (unsigned)final_ctr);
      }
      printf("|------|-----------|-----------|-----------|-----------|-----------|-------|\n");
    }
    return 0;
  }

  s32 BHUMD::close() {
    if (!mDevOpen)
      FATAL("NOT OPEN");
    close_bh_pcie_device();
    mDevOpen = false;
    return 0;
  }

  s32 BHUMD::deployRISCVCode() {
    bh_pcie_device_t* device = &mBHDev;
    // Deploy our RISCV machine code to the Tensix tile.
    memcpy(set_tlb_addr(0), rv_code, sizeof(rv_code));
    return 0;
  }


  s32 BHUMD::set_tlb_xy(unsigned x, unsigned y) {
    bh_pcie_device_t* device = &mBHDev;
    uint32_t c1 = device->tlb_cfg[1];
    uint32_t xy = (c1 & 0x7ff) + ((x & 0x3f) << 11) + ((y & 0x3f) << 17);
    if (xy != c1) {
      volatile uint32_t* tlb_reconfigure = device->tlb_reconfigure;
      tlb_reconfigure[1] = xy; // This is a slow UC write.
      device->tlb_cfg[1] = xy;
    }
    return 0;
  }

  char* BHUMD::set_tlb_addr(u64 addr) {
    bh_pcie_device_t* device = &mBHDev;
    // NB: The lo/mid/hi here are for PCIe 2 MiB TLBs. The on-device NIUs also have
    // fields with lo/mid/hi suffixes, but they use a totally different scheme.
    uint32_t addr_lo = (uint32_t)(addr & 0x1fffff);
    uint32_t addr_mid = (uint32_t)(addr >> 21);
    uint32_t addr_hi = (uint32_t)(addr >> 53);
    uint32_t c0 = device->tlb_cfg[0];
    uint32_t c1 = device->tlb_cfg[1];
    addr_hi += c1 & 0xfffff800; // Preserve the X/Y set by set_tlb_xy.
    char* result = device->tlb + addr_lo;
    volatile uint32_t* tlb_reconfigure = device->tlb_reconfigure;
    if (addr_mid != c0) {
      tlb_reconfigure[0] = addr_mid; // This is a slow UC write.
      device->tlb_cfg[0] = addr_mid;
    }
    if (addr_hi != c1) {
      tlb_reconfigure[1] = addr_hi; // This is a slow UC write.
      device->tlb_cfg[1] = addr_hi;
    }
    return result;
  }

  s32 BHUMD::configure_tlb() {
    if (set_tlb_xy(1, 2)) // Some Tensix tile; we don't really care which.
      FATAL("FAILED");
    // Put all RISCVs into reset, and configure their pc for coming out of reset.
    tlb_write_u32(RISCV_DEBUG_REG_SOFT_RESET_0, SOFT_RESET_ALL_RISCV);
    tlb_write_u32(RISCV_DEBUG_REG_TRISC0_RESET_PC, label_t0_setup);
    tlb_write_u32(RISCV_DEBUG_REG_TRISC1_RESET_PC, label_t1_setup);
    tlb_write_u32(RISCV_DEBUG_REG_TRISC2_RESET_PC, label_t2_setup);
    tlb_write_u32(RISCV_DEBUG_REG_NCRISC_RESET_PC, label_nc_setup);
    tlb_write_u32(RISCV_DEBUG_REG_TRISC_RESET_PC_OVERRIDE, ~0u);
    tlb_write_u32(RISCV_DEBUG_REG_NCRISC_RESET_PC_OVERRIDE, ~0u);
    return 0;
  }

  s32 BHUMD::open_bh_pcie_device() {
    char device_fn[20];
    sprintf(device_fn, "/dev/tenstorrent/%u", mDevNum);

    // Try to open the path.
    int fd = ::open(device_fn, O_RDWR | O_CLOEXEC);
    if (fd < 0) FATAL("Could not open device path '%s'", device_fn);

    // Confirm that it looks like a BH device.
    struct tenstorrent_get_device_info dev_info;
    memset(&dev_info, 0, sizeof(dev_info));
    dev_info.in.output_size_bytes = sizeof(dev_info.out);
    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &dev_info) < 0
        ||  dev_info.out.vendor_id != PCI_VENDOR_ID_TENSTORRENT
        ||  dev_info.out.device_id != PCI_DEVICE_ID_BLACKHOLE) {
      FATAL("Path '%s' does not seem to be a Tenstorrent Blackhole device", device_fn);
    }

    // We want a single 2 MiB TLB for accessing memory on the device.
    struct tenstorrent_allocate_tlb alloc_tlb;
    memset(&alloc_tlb, 0, sizeof(alloc_tlb));
    alloc_tlb.in.size = 1u << 21;
    if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc_tlb) < 0) {
      FATAL("Could not allocate a 2 MiB TLB on device '%s'; is tt-kmd too old?", device_fn);
    }

    // We want BAR0 for reconfiguring the TLB (tt-kmd has a reconfiguration syscall, but we prefer to do it ourselves).
    uint8_t resource_to_mapping[8] = {0};
    struct tenstorrent_mapping mappings[9];
    mappings[0].mapping_size = 8;
    if (ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings[0].mapping_size) >= 0) {
      for (unsigned i = 1; i < 9; ++i) {
        uint32_t resource = mappings[i].mapping_id;
        if (resource < 8) resource_to_mapping[resource] = i;
      }
    }
    mappings[0].mapping_size = 0;
    struct tenstorrent_mapping* bar0uc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE0_UC];
    if (bar0uc->mapping_size < TLB_CONFIG_ADDR_END) {
      FATAL("BAR0 on device '%s' is only %u bytes, which is less than the required %u bytes", device_fn,
            (unsigned)bar0uc->mapping_size, (unsigned)TLB_CONFIG_ADDR_END);
    }

    // Map the various pieces of memory.
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 1) page = 4096;
    size_t header_size = ((sizeof(bh_pcie_device_t) - 1) / (size_t)page + 1) * (size_t)page;
    size_t bar0_start = (TLB_CONFIG_ADDR / (size_t)page) * (size_t)page;
    size_t bar0_size = ((TLB_CONFIG_ADDR_END - bar0_start - 1) / (size_t)page + 1) * (size_t)page;
    size_t tlb_size = (((1u << 21) - 1) / (size_t)page + 1) * (size_t)page;
    size_t total_mmap_size = header_size + bar0_size + tlb_size;
    void* memory = mmap(NULL, total_mmap_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED
        ||  mprotect(memory, header_size, PROT_READ | PROT_WRITE) != 0
        ||  mmap((char*)memory + header_size, bar0_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, bar0uc->mapping_base + bar0_start) == MAP_FAILED
        ||  mmap((char*)memory + header_size + bar0_size, 1u << 21, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, alloc_tlb.out.mmap_offset_uc) == MAP_FAILED) {
      FATAL("Could not map memory for communicating with device '%s'", device_fn);
    }

    // Some TLB configuration we set once and never change; set that now.
    volatile uint32_t* tlb_reconfigure = (volatile uint32_t*)((char*)memory + header_size + (TLB_CONFIG_ADDR - bar0_start));
    if (alloc_tlb.out.id < 32) {
      tlb_reconfigure[(TLB_CONFIG_ADDR_STRIDES - TLB_CONFIG_ADDR) / sizeof(uint32_t) + alloc_tlb.out.id] = 0;
    }
    tlb_reconfigure += alloc_tlb.out.id * 3;
    tlb_reconfigure[2] = (1u << 6); // TLB_CFG_STRICT_AXI

    // Have everything we need; package it up and return it.
    bh_pcie_device_t* result = (bh_pcie_device_t*)&mBHDev;
    result->tlb_cfg[0] = tlb_reconfigure[0];
    result->tlb_cfg[1] = tlb_reconfigure[1];
    result->fd = fd;
    result->tlb_reconfigure = tlb_reconfigure;
    result->tlb = (char*)memory + header_size + bar0_size;
    result->host_page_size = (size_t)page;
    result->total_mmap_size = total_mmap_size;

    return 0;                     // success
  }

  s32 BHUMD::close_bh_pcie_device() {
    bh_pcie_device_t* device = &mBHDev;
    ::close(device->fd);
    munmap(device, device->total_mmap_size);
    return 0;
  }

}
