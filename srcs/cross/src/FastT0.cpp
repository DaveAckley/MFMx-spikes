#include "StandardLife.h" // For stepT0
#include "FastLocal.h"
#include "FastT0.h"
#include "ExtraConstants.h"
#include "FastT2.h" // for preloadT2Mailbox
#include "EP_LogBlock.h" // for theLogBlockL1Control
#include "Printf.h"
#include "CrossUtils.h"
#include "Debug.h"
#include "HartTasksLib.h" // for HTFuncPtr
#include "T6Phaser.h"

namespace MFM {
  extern "C" char __start_rodata_fp_table_t0[];
  extern "C" char __end_rodata_fp_table_t0[];

  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_LO = (volatile u32 *) 0xffb1'21f0u;
  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_HA = (volatile u32 *) 0xffb1'21f4u;
  static const volatile u32 * RISCV_DEBUG_REG_WALL_CLOCK_HI = (volatile u32 *) 0xffb1'21f8u;

  struct FastT0 {
    void init() {
      memset_s(this,'\0',sizeof(*this));
      copyHTFuncsT0();
    }

    static constexpr u32 MAX_HTFUNCS_T0 = 6u;
    HTFuncPtr mHTFuncsT0[MAX_HTFUNCS_T0];
    u8 mHTFuncsInUseT0;

    void copyHTFuncsT0() {
      LOGMARK;

      HTFuncPtr* start_addr = (HTFuncPtr*) &__start_rodata_fp_table_t0;
      HTFuncPtr* end_addr = (HTFuncPtr*) &__end_rodata_fp_table_t0;

      u32 ptrCount = end_addr - start_addr;
      HBPTAG(ptrC,ptrCount);
      MFM_API_ASSERT(ptrCount < MAX_HTFUNCS_T0,OUT_OF_ROOM);
      for (u32 i = 0u; i < ptrCount; ++i) 
        mHTFuncsT0[i] = start_addr[i];
      mHTFuncsInUseT0 = (u8) ptrCount;
    }

    void stepHTFuncsT0() {
      EACH(10'000,HBPTAG(stepT0,__EACHNUM__));
      for (u32 j = 0u; j < mHTFuncsInUseT0; ++j) {
        u32 i = j;
        HTFuncPtr epf = mHTFuncsT0[i];
        SNAP(2,HBPTAG(doT0,(void*) epf));
        if (epf) {
          (*epf)(HTOC_LIVE);
        }
        SNAP(2,HBPTAG(didT0,(void*) epf));
      }
    }
    
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

  u32 millisElapsed() {
    memoryFence();
    return totalMillisElapsed;
  }

  u32 ticksElapsed() {
    memoryFence();
    return t0TicksElapsed;
  }

  void stepT0(HostBlock & hb) {

    u32 aiFreq = hb.mAIClockFrequency;

    u64 now = FastT0::readDebugTimestamper(); // pound away at the timestamper!
    u64 cycles = now - fT0.debugTimestamperStart;

    u32 ticksElapsed = (u32) (cycles>>23u); // 200MHz-> ~24Hz, 800MHz-> ~95Hz, 1235MHZ-> ~147Hz
    
    if (aiFreq != 0u) {
      fT0.newMillisElapsed = (u32) ((1000 * cycles) / aiFreq);
      if (fT0.newMillisElapsed != fT0.lastMillisElapsed) { // don't hit L1 til new milli
        fT0.lastMillisElapsed = fT0.newMillisElapsed;
        totalMillisElapsed = fT0.newMillisElapsed;
        aiFreq = hb.mAIClockFrequency; // and refresh aiFreq then too, just in case
        // CALL THE T0 STEPFUNCS ONCE PER ~MILLI!
        fT0.stepHTFuncsT0();
      }

      if (fT0.debugTicksElapsed != ticksElapsed) {
        const u32 LIM = 1<<14;
        if (ticksElapsed % LIM == 0) { // >~8s -> >~5.5s
          HBPTAG(16kticks,ticksElapsed/LIM);
          LOGPTAG(16kticksl,ticksElapsed/LIM);
        }
        fT0.debugTicksElapsed = ticksElapsed;
        t0TicksElapsed = ticksElapsed; // for the neighbors
      }
    }
  }

  // called by initseq and by stepHTFuncsT0
  RCFlag manageLogBlockT0(HTOpCode htoc) {
    RCFlag ret = RCFlag::RC_ZERO;
    if (unlikely(htoc == HTOpCode::HTOC_INIT)) {

      HBMARK;
      theLogBlockL1Control.init();
      HBMARK;
      //ret = true;

    } else if (unlikely(htoc == HTOpCode::HTOC_OPEN)) {
      LOGMARK;
    } else if (likely(htoc == HTOpCode::HTOC_LIVE)) {

      static u32 spin = 0;
      //// LIFE
      static constexpr u32 BITS = 13;
      if ((spin++ & ((1u<<BITS)-1)) == 0) {
        LOGPTAG(LBStep,spin>>BITS);
      }

      HostBlock & hb = theHostBlock;
      theLogBlockL1Control.step(hb);      
    } else LOGPTAG(unknown htoc,htoc);

    return ret;
  }

  __attribute__((section(".rodata_fp_table_t0")))
  HTFuncPtr logT0 = &manageLogBlockT0;

  ////////
  TEFResult TaskEpochFunction_CLOK(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    if (hartnum != HARTNUM_T0) return TEFR_NO_THANKS;
    switch (hei) {
    case HE_BGN:
      HBNOTE(init CLOK);
      fT0.init();
      fT0.debugTimestamperStart = FastT0::readDebugTimestamper();
      fT0.debugTicksElapsed = 0u; // 0 init to suppress KT 0.000 reports
      t0TicksElapsed = 0u;
      return TEFR_CONTINUE;

    default:
      break;
    }
    return TEFR_NO_THANKS;
  }


}



