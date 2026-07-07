#pragma once        /* -*- C++ -*- */
#include "itype.h"
#include "Constants.h"
#include "AtomicLock.h"

namespace MFM {

  static constexpr u8 HM__ = 0;
  static constexpr u8 HM_b = 1<<HARTNUM_B;
  static constexpr u8 HM_0 = 1<<HARTNUM_T0;
  static constexpr u8 HM_1 = 1<<HARTNUM_T1;
  static constexpr u8 HM_2 = 1<<HARTNUM_T2;
  static constexpr u8 HM_n = 1<<HARTNUM_NC;
  static constexpr u8 HM_b01 = HM_b|HM_0|HM_1;
  static constexpr u8 HM_all = HM_b|HM_0|HM_1|HM_2|HM_n;
  
#define ALL_HART_EPOCHS \
  XX(BEGIN)             \
  XX(BORN)              \
  XX(GROW)              \
  XX(LIVE)              \
  XX(RSRV)              \

#define ALL_HART_TASKS                     \
  /* BEG BOR GRO LIV RSV */                \
  XX(  b,  _,  _,  _,  _, BOOT)            \
  XX(  0,  _,  _,  _,  _, CLOCK)           \
  XX(  _,  _,  0,all,  _, LOG)             \
  XX(  2,b01,  _,  2,b01, PRNG)            \
  XX(  n,  n,  _,  n,  _, NOC)             \
  
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

#define XX(BE,BI,YO,LI,DI,N) +1
  constexpr u8 HART_TASK_COUNT = 1
    ALL_HART_TASKS
    ;
#undef XX
  
#define XX(BE,BI,YO,LI,DI,N) ,HT_##N
  enum HartTaskIndex : u8 {
    HT_ILLEGAL=0                // blow 0 for OoB
    ALL_HART_TASKS
  };
#undef XX

  enum TEFResult : u8 {
    TEFR_CONTINUE,              // this step completed, continue init seq
    TEFR_HART_OUT               // init seq done for this hart
  };
  using TaskEpochFunc = TEFResult (HartTaskIndex hti, HartEpochIndex hei, u8 hartnum);
  using TaskEpochFuncPtr = TaskEpochFunc*;

  // a TaskEpochFunction_FOO must be defined somewhere for every task FOO
#define XX(BE,BI,YO,LI,DI,N) extern TaskEpochFunc TaskEpochFunction_##N;
  ALL_HART_TASKS
#undef XX

#define XX(BE,BI,YO,LI,DI,N) extern TaskEpochFunc TaskEpochFunction_##N;
  ALL_HART_TASKS
#undef XX
  
  extern TaskEpochFuncPtr hartTaskEpochFunctions[HART_TASK_COUNT];

  extern const u8 hartTaskTable[HART_TASK_COUNT][HART_EPOCH_COUNT];
  
  extern const char *hartTaskNames[HART_TASK_COUNT];

  inline const char * getHartTaskName(HartTaskIndex hti) {
    if (hti >= HART_TASK_COUNT) hti = HartTaskIndex::HT_ILLEGAL;
    return hartTaskNames[hti];
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

  struct HartTaskerPublicState {
    AtomicLock mLock;
    RCFlag mRCFlags;
    u8 mEpochTaskDoneFlags[HART_TASK_COUNT][HART_EPOCH_COUNT];
    u8 mEpochColumn;
    u8 mHartsIn;
    u8 mHartsOut;

    void init() ;
  };

  extern HartTaskerPublicState theHartTaskerPublicState; // in t6main.cpp

  struct HartTaskerPrivate {
    void init(HartTaskerPublicState & ps) ;
    void run() ;

    HartTaskerPublicState * mPubState;
  };
}
