#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "OurTLBs.h"
#include "CodeManager.h"
#include "Constants.h"

#include "P2PElevator.h"
#include "TransportBlock.h"

#include "test10-exports.h"

namespace MFM {
  struct DemoCar {
    u8 mBytes[256];
  };
  typedef P2PElevatorPlatform<DemoCar,2> MyPlatform;
  static MyPlatform myPlatform;
}
int main() {
  printf("T6-TRANSPO-INFO at 0x%08x, len %u/0x%x\n",
         MFM::T6::transportblock_start,
         MFM::T6::transportblock_size,
         MFM::T6::transportblock_size);

  printf("sizeof(myPlatform) = %lu\n",sizeof(MFM::myPlatform));
  printf("sizeof(payload) = %lu\n",sizeof(MFM::MyPlatform::Payload));
  int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
  ASSERT(fd >= 0);
  
  MFM::OurTLBs ourTLBs(fd);
  ourTLBs.allocateTLBs();
  ourTLBs.configureTLBs();
  printf("------------Allocate host buffer space\n");
  ourTLBs.allocateHostRAM((1<<13)*(MFM::OurTLBs::AHAX_TLBI_L1_LAST_UNI+1u));
  //ourTLBs.allocateHostRAM(4096u);
  //ourTLBs.allocateHostRAM(1<<21u);
  printf("  allocated %lu at %p for noc 0x%lx\n",
         ourTLBs.hostRAMSize(),
         ourTLBs.hostRAMPtr(),
         ourTLBs.hostRAMNocAddr());

  printf("------------Put all cores in soft reset\n");
  ourTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                  RISCV_DEBUG_REG_SOFT_RESET_0,
                  SOFT_RESET_ALL_RISCV);

  printf("------------Set starting addresses\n");
  const MFM::u32 data[][2] = {
    {RISCV_DEBUG_REG_TRISC0_RESET_PC, 4u},
    {RISCV_DEBUG_REG_TRISC1_RESET_PC, 8u},
    {RISCV_DEBUG_REG_TRISC2_RESET_PC, 12u},
    {RISCV_DEBUG_REG_NCRISC_RESET_PC, 16u},
    {RISCV_DEBUG_REG_TRISC_RESET_PC_OVERRIDE, 0x1|0x2|0x4}, // use custom start addrs for T0,T1,T2
    {RISCV_DEBUG_REG_NCRISC_RESET_PC_OVERRIDE, 0x1},        // use custom start addr for NCRISC
  };
  printf("DATA %lu SIZ %lu\n",sizeof(data),sizeof(data[0]));

  for (MFM::u32 i = 0u; i < sizeof(data)/sizeof(data[0]); ++i) {
    printf("%u 0x%08x = 0x%08x\n",i,data[i][0],data[i][1]);
    ourTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI, data[i][0], data[i][1]);
  }

  printf("------------Deploy the code\n");
  MFM::CodeManager cmgr(ourTLBs);
  cmgr.deployRISCVCodeFromFile("../cross/bin/test10.bin");

  printf("------------Release the hound( leader)s\n");
  ourTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                  RISCV_DEBUG_REG_SOFT_RESET_0,
                  SOFT_RESET_ALL_RISCV_EXCEPT_B);

  printf("------------Await results\n");
  cmgr.awaitResults();

  // search for magic delivered data
  MFM::u32 hits = 0u;
  MFM::u32 * base = (MFM::u32 *) ourTLBs.hostRAMPtr();
  for (MFM::u32 tlbi = MFM::OurTLBs::AHAX_TLBI_L1_FIRST_UNI;
       tlbi <= MFM::OurTLBs::AHAX_TLBI_L1_LAST_UNI;
       ++tlbi) {
    MFM::u32 * p = base + ((tlbi * sizeof(MFM::TransportBlock))>>2u);
    if (p[3] == 0xf00baa9) ++hits;
    else if (true) printf("NOTMAGIC %u %p == 0x%08x\n",tlbi,p,p[0]);
  }
  printf("MAGIC SEARCH HITS %d\n",hits);

  // 2: release the hounds
  // 3: wait for certain addresses to be in 'postrun' state
  // 4: be haphaphappy
  
  return 0;
}
