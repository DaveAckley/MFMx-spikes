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

    u8 spin = 0u;
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      u64 now = FastT0::readDebugTimestamper(); // pound away at the timestamper!
      u64 cycles = now - fT0.debugTimestamperStart;
      u32 ticksElapsed = (u32) (cycles>>26u); // 200MHz-> ~3Hz, 800MHz-> ~12Hz, 1235MHZ-> ~18Hz
      if (hb.mAIClockFrequency != 0u)
        totalMillisElapsed = (u32) ((1000 * cycles) / hb.mAIClockFrequency);
      { static u32 once = 0;
        if (once < 60) {
          if (totalMillisElapsed >= 1000*once) {
            DP.printf("%d T0#%d %uMHz %us %u ticks",
                      once,hb.mTLBI,hb.mAIClockFrequency/1'000'000u,totalMillisElapsed/1000u,t0TicksElapsed);
            once += between(1u,10u);
          }
        }
      }
      if (fT0.debugTicksElapsed != ticksElapsed) {
        if (ticksElapsed % 1000u == 0) { // ~8s -> ~5.5s
          LOG.printf("%s:KT %u.%03u\n",
                     hartName(fAll.mHartNum),ticksElapsed/1000,ticksElapsed%1000);
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
