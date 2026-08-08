#pragma once        /* -*- C++ -*- */
#include "itype.h"
#include "Constants.h"
#include "AtomicLock.h"

namespace MFM {

#define ALL_HART_EPOCHS  \
  XX(BEGIN)              \
  XX(BORN0)              \
  XX(BORN1)              \
  XX(GROW0)              \
  XX(GROW1)              \
  XX(GROW2)              \
  XX(GROW3)              \
  XX(LIVE)               \

  // LIB_HART_TASKS is used by allimg/include/HartTasks.h, where it is
  // merged with IMAGE_HART_TASKS to create ALL_HART_TASKS, but is
  // defined here because these are the lib-associated tasks
#define LIB_HART_TASKS                         \
  /* BZOTN -> hb,h0,h1,h2,hn, with UC & lc */  \
  /* BEG BO0 BO1 GR0 GR1 GR2 GR3 LIV TASK */   \
  XX(  b,  _,  _,  _,  _,  _,  _,  _, BOOT)    \
  XX(  z,  _,  _,  _,  _,  _,  _,  _, CLOCK)   \
  XX(  _,  _,  z,all,  _,  _,  _,  _, LOG)     \
  XX(  _,  t,bzo,  t,bzo,  _,  _,  _, PRNG)    \
  XX(  n,  n,  n,  _,  _,  n,  _,  _, NOC)     \

#define XX(N) +1
  constexpr u8 HART_EPOCH_COUNT = 1
    ALL_HART_EPOCHS
    ;
#undef XX

#define XX(N) ,HE_##N
  enum HartEpochIndex : u8 {
    HEI_ILLEGAL=0                // blow 0 for OoB
    ALL_HART_EPOCHS
  };
#undef XX

  extern const char *hartEpochNames[HART_EPOCH_COUNT];

  inline const char * getHartEpochName(HartEpochIndex hei) {
    if (hei >= HART_EPOCH_COUNT) hei = HartEpochIndex::HEI_ILLEGAL;
    return hartEpochNames[hei];
  }

  enum HTOpCode : u8 {
    HTOC_ILLEGAL = 0,
    HTOC_INIT,
    HTOC_OPEN,
    HTOC_LIVE
  };

  enum RCFlag : u32 {
    RC_ZERO =             0x00000000,
    RC_HB_INITTED =       1,
    RC_HBMARK_INITTED =   RC_HB_INITTED<<1,
    RC_HBMARK_RUNNING =   RC_HBMARK_INITTED<<1,
    RC_CLOCK_INITTED =    RC_HBMARK_RUNNING<<1,
    RC_PRNG_INITTED =     RC_CLOCK_INITTED<<1,
    RC_NOC_INITTED =      RC_PRNG_INITTED<<1,
    RC_IMAGE1_INITTED =   RC_NOC_INITTED<<1,

    RC_N_STARTED =     0x00000020,
    RC_B_SELF_UP =     0x00000040,
    RC_0_SELF_UP =     0x00000080,
    RC_1_SELF_UP =     0x00000100,
    RC_2_SELF_UP =     0x00000200,
    RC_N_SELF_UP =     0x00000400,
    RC_B_SERVICES_UP = 0x00000800,
    RC_0_SERVICES_UP = 0x00001000,
    RC_1_SERVICES_UP = 0x00002000,
    RC_2_SERVICES_UP = 0x00004000,
    RC_N_SERVICES_UP = 0x00008000
  };

  using HTFuncPtr = RCFlag (*)(HTOpCode htoc);

  enum TEFResult : u8 {
    TEFR_CONTINUE,              // this step completed, continue init seq
    TEFR_HART_OUT               // init seq done for this hart
  };

  enum HartEpochIndex : u8;     // FORWARD
  enum HartTaskIndex : u8;      // FORWARD
  using TaskEpochFunc = TEFResult (HartTaskIndex hti, HartEpochIndex hei, u8 hartnum);
  using TaskEpochFuncPtr = TaskEpochFunc*;

}
