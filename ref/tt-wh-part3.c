#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define FATAL(fmt, ...) do {fprintf(stderr, fmt " (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__); exit(1);} while(0)
#define ASSERT(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)

#define TENSTORRENT_IOCTL_QUERY_MAPPINGS 0xFA02

struct tenstorrent_mapping {
  uint32_t mapping_id;
  uint32_t reserved;
  uint64_t mapping_base;
  uint64_t mapping_size;
};

#define TENSTORRENT_MAPPING_RESOURCE0_UC 1
#define TENSTORRENT_MAPPING_RESOURCE0_WC 2
#define TENSTORRENT_MAPPING_RESOURCE2_UC 5

#define BAR0_WC_SIZE (464 << 20)
#define BAR0_SIZE    (496 << 20)
#define MMAP_SIZE    (512 << 20)

#define BAR4_SOC_TARGET_ADDRESS 0x1E000000

#define TLB_CONFIG_ADDR 0x1FC00000

#define TLB_IDX_0 0
#define TLB_IDX_UC0 184

#define TLB_CFG_UNICAST(x, y) (((y) << 6) + (x))
#define TLB_CFG_MULTICAST(x_start, y_start, x_end, y_end) ((1 << 25) + ((y_start) << 18) + ((x_start) << 12) + ((y_end) << 6) + (x_end))
#define TLB_CFG_NOC1 (1 << 24)

static char* set_tlb(char* dev, uint32_t idx, uint64_t cfg, uint32_t suitable_for_addr) {
  char* result = dev;
  uint32_t abits;
  if (idx < 156) {
    abits = 20;
    result += (idx << 20);
  } else if (idx < 166) {
    abits = 21;
    result += (156 << 20) + ((idx - 156) << 21);
  } else {
    abits = 24;
    result += (156 << 20) + (10 << 21) + ((idx - 166) << 24);
  }
  cfg = (cfg << (36 - abits)) + (suitable_for_addr >>= abits);
  ((volatile uint64_t*)(dev + TLB_CONFIG_ADDR))[idx] = cfg;
  return result - (suitable_for_addr << abits);
}

#define RV_ADDR_SOFT_RESET 0xFFB121B0

#define SOFT_RESET_ALL_CORES 0x47800

int main() {
  int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
  ASSERT(fd >= 0);

  unsigned char resource_to_mapping[8] = {0};
  struct tenstorrent_mapping mappings[sizeof(resource_to_mapping) + 1];
  mappings[0].mapping_size = sizeof(resource_to_mapping);
  ASSERT(ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings[0].mapping_size) >= 0);
  mappings[0].mapping_size = 0;
  for (unsigned i = 1; i <= sizeof(resource_to_mapping); ++i) {
    uint32_t resource = mappings[i].mapping_id;
    if (resource < sizeof(resource_to_mapping)) {
      resource_to_mapping[resource] = i;
    }
  }

  struct tenstorrent_mapping* bar0uc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE0_UC];
  struct tenstorrent_mapping* bar0wc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE0_WC];
  struct tenstorrent_mapping* bar4uc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE2_UC];
  ASSERT(bar0uc->mapping_size >= BAR0_SIZE);
  ASSERT(bar4uc->mapping_size >= MMAP_SIZE - BAR4_SOC_TARGET_ADDRESS);

  char* dev = mmap(NULL, MMAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT(dev != MAP_FAILED);

  uint32_t wc_size = bar0wc->mapping_size;
  if (wc_size) {
    if (wc_size > BAR0_WC_SIZE) {
      wc_size = BAR0_WC_SIZE;
    }
    if (mmap(dev, wc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, bar0wc->mapping_base) == MAP_FAILED) {
      wc_size = 0;
    }
  }
  ASSERT(mmap(dev + wc_size, BAR0_SIZE - wc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, bar0uc->mapping_base + wc_size) != MAP_FAILED);
  ASSERT(mmap(dev + BAR0_SIZE, MMAP_SIZE - BAR0_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, bar4uc->mapping_base + (BAR0_SIZE - BAR4_SOC_TARGET_ADDRESS)) != MAP_FAILED);

  // Multicast the soft reset
  char* reg_tlb = set_tlb(dev, TLB_IDX_UC0, TLB_CFG_MULTICAST(0, 0, 9, 11), RV_ADDR_SOFT_RESET);
  *(volatile uint32_t*)(reg_tlb + RV_ADDR_SOFT_RESET) = SOFT_RESET_ALL_CORES;

  // Multicast the code
  const uint32_t rv_code[] = {
    0xFFB12537, // lui a0, 0xFFB12
    0x1F052583, // lw a1, 0x1F0(a0)
    0x1F852603, // lw a2, 0x1F8(a0)
    0x08B02023, // sw a1, 128(x0)
    0x08C02223, // sw a2, 132(x0)
    0x0000006F, // loop: j loop
  };
  char* l1_tlb = set_tlb(dev, TLB_IDX_0, TLB_CFG_MULTICAST(0, 0, 9, 11), 0);
  memcpy(l1_tlb, rv_code, sizeof(rv_code));

  uint64_t times[2][8][8];
  for (uint32_t noc = 0; noc < 2; ++noc) {
    // Bring one core out of reset on each tile
    reg_tlb = set_tlb(dev, TLB_IDX_UC0, TLB_CFG_MULTICAST(0, 0, 9, 11) + noc * TLB_CFG_NOC1, RV_ADDR_SOFT_RESET);
    *(volatile uint32_t*)(reg_tlb + RV_ADDR_SOFT_RESET) = SOFT_RESET_ALL_CORES & (SOFT_RESET_ALL_CORES - 1);

    // Collect recordings
    for (uint32_t y = 0; y < 8; ++y) {
      for (uint32_t x = 0; x < 8; ++x) {
        l1_tlb = set_tlb(dev, TLB_IDX_0, TLB_CFG_UNICAST(18 + x, 18 + y), 0);
        times[noc][y][x] = *(volatile uint64_t*)(l1_tlb + 128);
      }
    }

    // Back into soft reset
    *(volatile uint32_t*)(reg_tlb + RV_ADDR_SOFT_RESET) = SOFT_RESET_ALL_CORES;
  }

  // Per-column counter adjustment
  uint64_t adj[8];
  uint64_t adj_min = ~0ull;
  for (uint32_t x = 0; x < 8; ++x) {
    uint64_t val = adj[x] = (times[0][2][x] + times[1][2][x]) / 2;
    if (val < adj_min) adj_min = val;
  }
  printf("-- Adjustments:\n");
  for (uint32_t x = 0; x < 8; ++x) {
    printf("-%u, ", (unsigned)(adj[x] -= adj_min));
  }
  printf("\n");

  // Print the measurements
  for (uint32_t noc = 0; noc < 2; ++noc) {
    printf("-- NoC %u:\n", noc);
    uint64_t min_t = ~0ull;
    for (uint32_t y = 0; y < 8; ++y) {
      for (uint32_t x = 0; x < 8; ++x) {
        uint64_t t = (times[noc][y][x] -= adj[x]);
        if (t < min_t) min_t = t;
      }
    }
    for (uint32_t y = 0; y < 8; ++y) {
      printf("{");
      for (uint32_t x = 0; x < 8; ++x) {
        printf("%u, ", (unsigned)(times[noc][y][x] - min_t));
      }
      printf("},\n");
    }
  }
}