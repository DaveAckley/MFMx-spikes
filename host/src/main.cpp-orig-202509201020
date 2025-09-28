#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// use the source AHAX ?
#include "ioctl.h" // from code/D/tt-metal/tt_metal/third_party/umd/device?

#define FATAL(fmt, ...) do {fprintf(stderr, fmt " (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__); exit(1);} while(0)
#define ASSERT(cond) ASSERT_DBG(cond)
#define ASSERT_DBG(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)
#define ASSERT_NONDBG(cond) (cond)

int main() {
  int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
  ASSERT(fd >= 0);

  unsigned char resource_to_mapping[10] = {0};
  memset(resource_to_mapping,0,sizeof(resource_to_mapping));
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

  for (unsigned i = 0; i < sizeof(resource_to_mapping); ++i) {
    printf("[%d/%d/%d: 0x%llx+%lld/0x%llx] ",i,
           mappings[i].mapping_id,
           resource_to_mapping[i],
           mappings[i].mapping_base,
           mappings[i].mapping_size,
           mappings[i].mapping_size);
  }
  printf("\n");

  struct tenstorrent_mapping* bar0uc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE0_UC];
  struct tenstorrent_mapping* bar0wc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE0_WC];
  struct tenstorrent_mapping* bar4uc = mappings + resource_to_mapping[TENSTORRENT_MAPPING_RESOURCE2_UC];

#define BAR0_WC_SIZE (464 << 20)
#define BAR0_SIZE    (496 << 20)
#define MMAP_SIZE    (512 << 20)
  
#define BAR4_SOC_TARGET_ADDRESS 0x1E000000

  ASSERT(bar0uc->mapping_size >= BAR0_SIZE);
  ASSERT(bar4uc->mapping_size >= MMAP_SIZE - BAR4_SOC_TARGET_ADDRESS);

  
  tenstorrent_mapping foo;
  return 1;
}
