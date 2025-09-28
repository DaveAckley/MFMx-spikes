#ifndef TTKMDSTUFF_H        /* -*- C++ -*- */
#define TTKMDSTUFF_H

#include "Util.h"

namespace MFM {

#define TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA 2

  struct tenstorrent_allocate_dma_buf_in {
    uint32_t requested_size;
    uint8_t  buf_index; // [0,TENSTORRENT_MAX_DMA_BUFS)
    uint8_t  flags;
    uint8_t  reserved0[2];
    uint64_t reserved1[2];
  };

  struct tenstorrent_allocate_dma_buf_out {
    uint64_t physical_address; // or IOVA
    uint64_t mapping_offset;
    uint32_t size;
    uint32_t reserved0;
    uint64_t noc_address; // valid if TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA is set
    uint64_t reserved1;
  };

  struct tenstorrent_allocate_dma_buf {
    struct tenstorrent_allocate_dma_buf_in  in;
    struct tenstorrent_allocate_dma_buf_out out;
  };

  struct tenstorrent_noc_tlb_config {
    uint64_t addr;
    uint16_t x_end;
    uint16_t y_end;
    uint16_t x_start;
    uint16_t y_start;
    uint8_t noc;
    uint8_t mcast;
    uint8_t ordering;
    uint8_t linked;
    uint8_t static_vc;
    uint8_t reserved0[3];
    uint32_t reserved1[2];
  };

  struct tenstorrent_configure_tlb_in {
    uint32_t id;
    struct tenstorrent_noc_tlb_config config;
  };

  struct tenstorrent_configure_tlb_out {
    uint64_t reserved;
  };

  struct tenstorrent_configure_tlb {
    struct tenstorrent_configure_tlb_in in;
    struct tenstorrent_configure_tlb_out out;
  };

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

  //#define TENSTORRENT_MAPPING_RESOURCE0_UC 1

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

  typedef struct pinned_host_buffer_t {
    size_t size;
    void* host_ptr;
    uint64_t noc_addr;
  } pinned_host_buffer_t;

#define TENSTORRENT_PIN_PAGES_NOC_DMA      2 // app wants to use the pages for NOC DMA
#define TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN 4

  struct tenstorrent_pin_pages_in {
    uint32_t output_size_bytes;
    uint32_t flags;
    uint64_t virtual_address;
    uint64_t size;
  };

  struct tenstorrent_pin_pages_out_extended {
    uint64_t physical_address;
    uint64_t noc_address;
  };

  struct tenstorrent_pin_pages_extended {
    struct tenstorrent_pin_pages_in in;
    struct tenstorrent_pin_pages_out_extended out;
  };

}


#endif /* TTKMDSTUFF_H */
