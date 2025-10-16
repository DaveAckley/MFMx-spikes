#include <fcntl.h>
#include <thread>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <inttypes.h>

#include "OurTLBs.h"
#include "CodeManager.h"
#include "Constants.h"

#include "P2PElevator.h"
#include "TransportBlock.h"
#include "HostBlock.h"
#include "HostUtils.h"

#include "t6-exports.h"

namespace MFM {
}

int spikeMain() {
  printf("PHASE-------Pre-startup\n");
  printf("T6-TRANSPO-INFO at 0x%08x, len %u/0x%x\n",
         MFM::T6::transportblock_all_start,
         MFM::T6::transportblock_all_size,
         MFM::T6::transportblock_all_size);

  printf("LogCarStoragePtr is at 0x%08x\n", MFM::T6::transportblock_log_start);
  printf("EWCarStoragePtr is at 0x%08x\n", MFM::T6::transportblock_ew_start);

  printf("sizeof(HostBlock) = %lu\n",sizeof(MFM::HostBlock));
  //  printf("sizeof(myPlatform) = %lu\n",sizeof(MFM::myPlatform));
  //  printf("sizeof(payload) = %lu\n",sizeof(MFM::MyPlatform::Payload));
  int fd = open("/dev/tenstorrent/0", O_RDWR | O_CLOEXEC);
  ASSERT(fd >= 0);
  
  MFM::OurTLBs ourTLBs;
  ourTLBs.setDeviceInfo(0u,fd);
  ourTLBs.allocateTLBs();
  ourTLBs.configureTLBs();
  printf("PHASE-------Allocate host buffer space\n");
  ourTLBs.allocateHostRAM(1<<13); // size per T6
  //ourTLBs.allocateHostRAM(4096u);
  //ourTLBs.allocateHostRAM(1<<21u);
  printf("  allocated %lu at %p for noc 0x%lx\n",
         ourTLBs.hostRAMSize(),
         ourTLBs.hostRAMPtr(),
         ourTLBs.hostRAMNocAddr());

  printf("PHASE-------Init host transport platforms\n");
  MFM::LogCarStorage::LogCar *c1 = ourTLBs.getLogCarHost(20,0);
  MFM::u32 t6addr = ourTLBs.getLogCarT6(20,0);
  printf("  logcar(20,1) is at %p remote %d/0x%08x\n",c1,20,t6addr);

  bool quitTransport = false;
  auto wtf = std::thread([&ourTLBs,&quitTransport]() {
    for (MFM::u64 i = 0u; ++i != 0u; ) {
      if (quitTransport) {
        printf("\n\n YAMINDA EXTERNAL QUITZOS BAHT (%" PRIu64 ")\n\n",i);
        break;
      }
      const MFM::u64 aBILLION = 1'000'000'000ul;
      if (i % aBILLION == 0)
        printf("\n\n YAMINDA transport thread yo %uG %p\n\n",(MFM::u32) (i/aBILLION),  &ourTLBs);
      ourTLBs.updateLogTransports();
    }
  });

  printf("PHASE-------Put all cores in soft reset\n");
  ourTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                  RISCV_DEBUG_REG_SOFT_RESET_0,
                  SOFT_RESET_ALL_RISCV);

  printf("PHASE-------Set starting addresses\n");
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

  printf("PHASE-------Deploy the code\n");
  MFM::CodeManager cmgr(ourTLBs);
  //  cmgr.deployRISCVCodeFromFile("../cross/bin/t6main.bin"); // OLDE WAIYE
  cmgr.deployRISCVCodeFromFile("./build_cross/bin/crossmain.bin"); // NEW CMAKE WAY GNU

  printf("PHASE-------Release the hound( leader)s\n");
  ourTLBs.write32(MFM::OurTLBs::AHAX_TLBI_DEBUG_MULTI,
                  RISCV_DEBUG_REG_SOFT_RESET_0,
                  SOFT_RESET_ALL_RISCV_EXCEPT_B);

  MFM::sleepUsec(100'000);
  printf("PHASE-------Check magic\n");
  cmgr.assertGoodMagic();

  printf("PHASE-------Await results\n");
  cmgr.awaitResults();

  /* Now obsoleted by actual LOG deliveries! 
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
  */

  printf("PHASE-------Recheck magic\n");
  cmgr.assertGoodMagic();

  quitTransport = true;
  wtf.join();
  return 0;
}
