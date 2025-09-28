#ifndef BHUMD_H          /* -*- mode: C++ -*- */
#define BHUMD_H

#include "Util.h"
#include "Constants.h"

namespace MFM {

struct tenstorrent_get_device_info_in {
  uint32_t output_size_bytes;
};
struct tenstorrent_get_device_info_out {
  uint32_t output_size_bytes;
  uint16_t vendor_id;
  uint16_t device_id;
  uint16_t subsystem_vendor_id;
  uint16_t subsystem_id;
  uint16_t bus_dev_fn;            // [0:2] function, [3:7] device, [8:15] bus
  uint16_t max_dma_buf_size_log2; // Since 1.0
  uint16_t pci_domain;            // Since 1.23
};
struct tenstorrent_get_device_info {
  struct tenstorrent_get_device_info_in in;
  struct tenstorrent_get_device_info_out out;
};

#define TENSTORRENT_MAPPING_RESOURCE0_UC 1

struct tenstorrent_mapping {
  uint32_t mapping_id;
  uint32_t reserved;
  uint64_t mapping_base;
  uint64_t mapping_size;
};

struct tenstorrent_allocate_tlb_in {
  uint64_t size;
  uint64_t reserved;
};
struct tenstorrent_allocate_tlb_out {
  uint32_t id;
  uint32_t reserved0;
  uint64_t mmap_offset_uc;
  uint64_t mmap_offset_wc;
  uint64_t reserved1;
};
struct tenstorrent_allocate_tlb {
  struct tenstorrent_allocate_tlb_in  in;
  struct tenstorrent_allocate_tlb_out out;
};

// Very thin user-mode driver, sufficient for poking around in device memory:

typedef struct bh_pcie_device_t {
  int fd;
  uint32_t tlb_cfg[2]; // Cached contents of *tlb_reconfigure, to avoid reconfiguration.
  volatile uint32_t* tlb_reconfigure;
  char* tlb; // 2 MiB window into device memory, configured using tlb_reconfigure.
  size_t host_page_size;
  size_t total_mmap_size;
} bh_pcie_device_t;

/** BlackHold User Mode Driver, thin corsix style
 */
class BHUMD {
 public:
  BHUMD(u32 deviceNumber) ;

  ~BHUMD() ;

  s32 open() ;
  s32 close() ;

  s32 deployRISCVCode() ;
  s32 doTests() ;

 private:
  s32 open_bh_pcie_device() ;
  s32 close_bh_pcie_device() ;

  s32 configure_tlb() ;
  s32 set_tlb_xy(unsigned x, unsigned y) ; // HERE'S WHERE (SINGLE) TENSIX CORE IS SELECTED
  char* set_tlb_addr(u64 addr) ;

  void tlb_write_u32(u64 addr, u32 value) ;
  u32 tlb_read_u32(u64 addr) ;

  u32 mDevNum;
  bool mDevOpen;
  bh_pcie_device_t mBHDev;
};

} // namespace MFM

#endif /* BHUMD_H */
