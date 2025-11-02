#include "FastLocal.h"
#include "FastT0.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "Printf.h"

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
  };
  FAST_LOCAL(FastT0,fT0,t0);

  u32 t0TicksElapsed;
  u32 totalMillisElapsed;

  static int liveT0(HostBlock & hb) __attribute__ ((optimize("O2")));

  int liveT0(HostBlock & hb) {
    fT0.debugTimestamperStart = FastT0::readDebugTimestamper();
    fT0.debugTicksElapsed = 0u; // 0 init to suppress KT 0.000 reports
    t0TicksElapsed = 0u;
    u32 stopTicks = between(50u,5000u);
    while (t0TicksElapsed < stopTicks) {
      u64 now = FastT0::readDebugTimestamper(); // pound away at the timestamper!
      u64 cycles = now - fT0.debugTimestamperStart;
      u32 ticksElapsed = (u32) (cycles>>26u); // 200MHz-> ~3Hz, 800MHz-> ~12Hz, 1235MHZ-> ~18Hz
      if (hb.mAIClockFrequency != 0u)
        totalMillisElapsed = (u32) (cycles / hb.mAIClockFrequency);
      if (fT0.debugTicksElapsed != ticksElapsed) {
        if (ticksElapsed % 100u == 0) { // ~8s -> ~5.5s
          DP.printf("%d[ticks%d] ",fAll.mHartNum,(u32) ticksElapsed);
          LOG.printf("%d:KT %u.%03u\n",fAll.mHartNum,ticksElapsed/1000,ticksElapsed%1000);
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
    DP.printf("T0#%d(%d,%d)\n",hb.mTLBI,hb.mXPos,hb.mYPos);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT0(hb);          // go do your hart t0 thing you
  }
}
