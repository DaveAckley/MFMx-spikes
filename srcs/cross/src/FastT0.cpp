#include "FastLocal.h"
#include "FastT0.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"
#include "CrossUtils.h"

namespace MFM {
  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_LO = (volatile u32 *) 0xffb1'21f0u;
  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_HA = (volatile u32 *) 0xffb1'21f4u;
  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_HI = (volatile u32 *) 0xffb1'21f8u;

  struct FastT0 {
    u64 debugTimestamperStart;
    u32 debugTicksElapsed;
    static u64 readDebugTimestamper() {
      u32 hi1, lo, hi2;
      do {
        hi1 = *RISCV_DEBUG_REG_WALL_CLOCK_HA;
        lo = *RISCV_DEBUG_REG_WALL_CLOCK_LO;
        hi2 = *RISCV_DEBUG_REG_WALL_CLOCK_HA;
      } while (hi1 != hi2);
      return (((u64) hi1)<<32u)|lo;
    }
    //u32 shadowImageBlock[4];
    //u8 shadowImageCode;
  };
  FAST_LOCAL(FastT0,fT0,t0);

  u32 t0TicksElapsed;
  u32 totalMillisElapsed;

  u32 millisElapsed() { return totalMillisElapsed; }

  static int liveT0(HostBlock & hb) /*__attribute__ ((optimize("O2")))*/;

  u32 lastMillisChange;
#if 0
  static void dumpIBOnChange() {
#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Warray-bounds"  // Aarrgh
    u32 * imgblock = (u32 *) 0x14;
    bool chg = false;
    for (u32 i = 0u; i < sizeof(fT0.shadowImageBlock)>>2; ++i) {
      if (fT0.shadowImageBlock[i] != imgblock[i]) {
        chg = true;
        break;
      }
    }
    if (chg) {
      u32 delta = totalMillisElapsed - lastMillisChange;
      for (u32 i = 0u; i < sizeof(fT0.shadowImageBlock)>>2; ++i) {
        u8 flag = (fT0.shadowImageBlock[i] != imgblock[i])?'*':' ';
        if (flag=='*') {
        DP.printf("%u.%03u+%u CIB#%d%c%x %x:%x\n",
                  totalMillisElapsed/1000u,
                  totalMillisElapsed%1000u,
                  delta,
                  fT0.shadowImageCode,
                  flag,
                  (u32) &imgblock[i],
                  fT0.shadowImageBlock[i],
                  imgblock[i]);
        fT0.shadowImageBlock[i] = imgblock[i];
        }
      }
      lastMillisChange = totalMillisElapsed;
    }
#pragma GCC diagnostic pop
  }
#endif

  int liveT0(HostBlock & hb) {
#if 0
#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Warray-bounds"  // Aarrgh
    {
      u32 * imgblock = (u32 *) 0x14;
      fT0.shadowImageCode = ((char*)&imgblock[0])[1];

      for (u32 i = 0u; i < sizeof(fT0.shadowImageBlock)>>2; ++i) {
        fT0.shadowImageBlock[i] = imgblock[i];
        DP.printf("T0:SIB[0x%x] = 0x%08x\n",(u32) &imgblock[i],fT0.shadowImageBlock[i]);
      }
      lastMillisChange = totalMillisElapsed;
    }
#pragma GCC diagnostic pop
#endif
    //DP.printf("T0:RND %d\n",create(100));
    fT0.debugTimestamperStart = FastT0::readDebugTimestamper();
    fT0.debugTicksElapsed = 0u; // 0 init to suppress KT 0.000 reports
    t0TicksElapsed = 0u;

    u16 spin = 0u;
    while (true) {
      //dumpIBOnChange(); // XXX Let's get right on this..
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      u64 now = FastT0::readDebugTimestamper(); // pound away at the timestamper!
      u64 cycles = now - fT0.debugTimestamperStart;
      //u32 ticksElapsed = (u32) (cycles>>22u); // 200MHz-> ~47Hz, 800MHz-> ~190Hz, 1235MHZ-> ~294Hz
      u32 ticksElapsed = (u32) (cycles>>23u); // 200MHz-> ~24Hz, 800MHz-> ~95Hz, 1235MHZ-> ~147Hz
      //u32 ticksElapsed = (u32) (cycles>>26u); // 200MHz-> ~3Hz, 800MHz-> ~12Hz, 1235MHZ-> ~18Hz
      if (hb.mAIClockFrequency != 0u)
        totalMillisElapsed = (u32) ((1000 * cycles) / hb.mAIClockFrequency);
      if (false) { static u32 once = 0;
        if (once < 60) {
          if (true && totalMillisElapsed >= 10000*once) {
            DP.printf("%d T0#%d %uMHz %us %u ticks\n",
                      once,hb.mTLBI,hb.mAIClockFrequency/1'000'000u,totalMillisElapsed/1000u,t0TicksElapsed);
            once += between(1u,10u);
          }
        }
      }
      if (fT0.debugTicksElapsed != ticksElapsed) {
        if (ticksElapsed % 1000u == 0) { // ~8s -> ~5.5s
          hb.addBytes('x','0'+(ticksElapsed/1000u)%10);
          /*LOG.printf("%s:KT %u.%03u\n",
            hartName(fAll.mHartNum),ticksElapsed/1000,ticksElapsed%1000);*/
        }
        fT0.debugTicksElapsed = ticksElapsed;
        t0TicksElapsed = ticksElapsed; // for the neighbors
      }
    }
    //    FAIL(USER_REQUESTED_FAILURE); // try to set T0's fail bit

    return 0;
  }

  int hartMainT0(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    preloadT2Mailbox();
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT0(hb);          // go do your hart t0 thing you
  }
}
