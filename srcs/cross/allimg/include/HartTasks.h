#pragma once        /* -*- C++ -*- */
#include "itype.h"
#include "Constants.h"
#include "AtomicLock.h"
#include "HartTasksLib.h"       /* for LIB_HART_TASKS etc */

namespace MFM {

  static constexpr u8 HA__ = 0; 
  static constexpr u8 HA_b = 1<<HARTNUM_B;    //  1
  static constexpr u8 HA_z = 1<<HARTNUM_T0;   //  2
  static constexpr u8 HA_o = 1<<HARTNUM_T1;   //  4
  static constexpr u8 HA_t = 1<<HARTNUM_T2;   //  8
  static constexpr u8 HA_n = 1<<HARTNUM_NC;   // 16
  static constexpr u8 HA_bn = HA_b|HA_n;
  static constexpr u8 HA_zo = HA_z|HA_o;
  static constexpr u8 HA_bzo = HA_b|HA_z|HA_o;
  static constexpr u8 HA_zon = HA_z|HA_o|HA_n;
  static constexpr u8 HA_all = HA_b|HA_z|HA_o|HA_t|HA_n;
  
#include "ImageHartTasks.inc"   /* must define IMAGE_HART_TASKS */

#define ALL_HART_TASKS LIB_HART_TASKS IMAGE_HART_TASKS

#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) +1
  constexpr u8 HART_TASK_COUNT = 1
    ALL_HART_TASKS
    ;
#undef XX

#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) ,HT_##NM
  enum HartTaskIndex : u8 {
    HT_ILLEGAL=0                // blow 0 for OoB
    ALL_HART_TASKS
  };
#undef XX

  // a TaskEpochFunction_FOO must be defined somewhere for every task FOO
#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) extern TaskEpochFunc TaskEpochFunction_##NM;
  ALL_HART_TASKS
#undef XX

  extern TaskEpochFuncPtr hartTaskEpochFunctions[HART_TASK_COUNT];

  extern const u8 hartTaskTable[HART_TASK_COUNT][HART_EPOCH_COUNT];

  extern const char *hartTaskNames[HART_TASK_COUNT];

  inline const char * getHartTaskName(HartTaskIndex hti) {
    if (hti >= HART_TASK_COUNT) hti = HartTaskIndex::HT_ILLEGAL;
    return hartTaskNames[hti];
  }

  struct HartTaskerPublicState {
    AtomicLock mLock;
    RCFlag mRCFlags;
    u8 mEpochTaskDoneFlags[HART_TASK_COUNT][HART_EPOCH_COUNT];
    u8 mEpochColumn;
    u8 mHartsIn;
    u8 mHartsOut;

    void init() ;
  };

  extern HartTaskerPublicState theHartTaskerPublicState; // in HartTasks.cpp

  struct HartTaskerPrivate {
    void init(HartTaskerPublicState & ps) ;
    void run() ;

    HartTaskerPublicState * mPubState;
  };

}
