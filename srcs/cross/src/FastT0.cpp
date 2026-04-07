#include "DefaultLives.h" // For stepT0
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
    u32 lastMillisElapsed;
    u32 newMillisElapsed;

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
  FAST_LOCAL(FastT0,fT0,0);

  u32 t0TicksElapsed;
  u32 totalMillisElapsed;

  u32 millisElapsed() { return totalMillisElapsed; }

  int liveT0(HostBlock & hb) {
    //DP.printf("T0:RND %d\n",create(100));
    fT0.debugTimestamperStart = FastT0::readDebugTimestamper();
    fT0.debugTicksElapsed = 0u; // 0 init to suppress KT 0.000 reports
    t0TicksElapsed = 0u;

    u16 spin = 0u;
    u32 aiFreq = hb.mAIClockFrequency;
    while (true) {
      if (++spin == 0) hb.hartbeat(fAll.mHartNum);
      u64 now = FastT0::readDebugTimestamper(); // pound away at the timestamper!
      u64 cycles = now - fT0.debugTimestamperStart;

      //u32 ticksElapsed = (u32) (cycles>>22u); // 200MHz-> ~47Hz, 800MHz-> ~190Hz, 1235MHZ-> ~294Hz
      u32 ticksElapsed = (u32) (cycles>>23u); // 200MHz-> ~24Hz, 800MHz-> ~95Hz, 1235MHZ-> ~147Hz
      //u32 ticksElapsed = (u32) (cycles>>26u); // 200MHz-> ~3Hz, 800MHz-> ~12Hz, 1235MHZ-> ~18Hz

      if (aiFreq != 0u) {
        fT0.newMillisElapsed = (u32) ((1000 * cycles) / aiFreq);
        if (fT0.newMillisElapsed != fT0.lastMillisElapsed) { // don't hit L1 til new milli
          fT0.lastMillisElapsed = fT0.newMillisElapsed;
          totalMillisElapsed = fT0.lastMillisElapsed;
          aiFreq = hb.mAIClockFrequency; // and refresh aiFreq then too, just in case
          // CALL STEPT0 ONCE PER MILLI!
          stepT0(hb);
        }
      }

      if (fT0.debugTicksElapsed != ticksElapsed) {
        if (ticksElapsed % 1000u == 0) { // ~8s -> ~5.5s
          hb.addBytes('x','0'+(ticksElapsed/1000u)%10);
        }
        fT0.debugTicksElapsed = ticksElapsed;
        t0TicksElapsed = ticksElapsed; // for the neighbors
      }
    }
    FAIL(UNREACHABLE_CODE); // um what? try to set T0's fail bit
  }

  int hartMainT0(HostBlock & hb) {
    MFM_API_ASSERT_ON_HART(HARTNUM_T0);
    preloadT2Mailbox();
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::LIVING; // announce entering event loop
    return liveT0(hb);          // go do your hart t0 thing you
  }
}
