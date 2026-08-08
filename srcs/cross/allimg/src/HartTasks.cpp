#include "HartTasks.h"
#include "CrossUtils.h"
#include "FastLocal.h"          // for fAll
#include "Debug.h"
#include "Printf.h"             // for snprintf

namespace MFM {

  const char * getTaskEpochName(char * buf, u32 len, HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    snprintf(buf,len,"%s->%s",
             getHartTaskName(hti),
             //             hartName(hartnum),
             getHartEpochName(hei));
    return buf;
  }

  static void TEFDie(HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) {
    char buf[30];
    HBPTAG(>>DYING: UNHANDLED TEF,getTaskEpochName(buf,30,hti,hei,hartnum));
    FAIL(UNSUPPORTED_OPERATION); 
  }

  /// Define weak task functions to fail if not overridden
#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM)                                  \
  TEFResult __attribute__((weak)) TaskEpochFunction_##NM (              \
                   HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) { \
    TEFDie(hti,hei,hartnum);                                            \
    return TEFR_HART_OUT; /* NOT REACHED */                             \
  }
  ALL_HART_TASKS
#undef XX

  /// Generate table of function ptrs
  const TaskEpochFuncPtr hartTaskEpochFunctionPtrs[HART_TASK_COUNT] = { 
    0 // illegal task
#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) ,&TaskEpochFunction_##NM
    ALL_HART_TASKS
#undef XX
  };

  /// Generate table of task names
#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) ,"" #NM
  const char *hartTaskNames[HART_TASK_COUNT] = {
    "illegal"
    ALL_HART_TASKS
  };
#undef XX

  /// Generate the main task table
  // generate first row 
#define XX(N) ,HA__
  const u8 hartTaskTable[HART_TASK_COUNT][HART_EPOCH_COUNT] = {
    { HA__ ALL_HART_EPOCHS } // blow 0th row
#undef XX
    
  // generate rest of rows
#define XX(BE,B0,B1,G0,G1,G2,G3,LV,NM) \
    ,{HA__, /*blow col0*/              \
      HA_##BE,                         \
      HA_##B0,                         \
      HA_##B1,                         \
      HA_##G0,                         \
      HA_##G1,                         \
      HA_##G2,                         \
      HA_##G3,                         \
      HA_##LV }                        \
    
    ALL_HART_TASKS
  };
#undef XX
  
  void HartTaskerPublicState::init() {
    memset_s(this,'\0',sizeof(*this));
    // -> flags = 0
    // -> column = 0
  }

  void HartTaskerPrivate::init(HartTaskerPublicState & ps) {
    memset_s(this,'\0',sizeof(*this));
    mPubState = &ps;
  }

  void HartTaskerPrivate::run() {
    MFM_API_ASSERT_NONNULL(mPubState);
    HartTaskerPublicState & ps = *mPubState;

    // Get my task codes and do them
    u8 me = fAll.mHartNum;
    u8 memask = 1<<me;
    while (true) {
      u8 epoch = ps.mEpochColumn;
      if (epoch >= HART_EPOCH_COUNT)
        break;                  // we done!
      for (u8 t = 0; t < HART_TASK_COUNT; ++t) {
        u8 whomask = hartTaskTable[t][epoch];
        EACH(100'000+10*me,{HBXX((u32)whomask);HBXX((u32)t);HBXX((u32)epoch);HBXX((u32)ps.mEpochTaskDoneFlags[t][epoch]);});
        if (0==(memask & whomask)) continue; // not my monkey
        if (0!=(memask & ps.mEpochTaskDoneFlags[t][epoch])) continue; // already done
        if ((whomask & ps.mHartsOut) != 0) { // ded if we need any out monkeys
          HBPTAG(dedWhen,getHartEpochName((HartEpochIndex) epoch));
          HBPTAG(dedWhat,getHartTaskName((HartTaskIndex) t));
          HBXX((u32) memask);
          HBXX((u32) whomask);
          HBXX((u32) ps.mHartsOut);
          HBASSERT_EQ((whomask & ps.mHartsOut),0);
        }
        // IT'S TIME FOR ME TO DO THIS EPOCH TASK
        {
          char buf[30];
          HBPTAG(+:,getTaskEpochName(buf,30,(HartTaskIndex) t, (HartEpochIndex) epoch, fAll.mHartNum));
        }
        TEFResult result = (*hartTaskEpochFunctionPtrs[t])((HartTaskIndex) t, (HartEpochIndex) epoch, fAll.mHartNum);
        // I HAVE DONE THIS EPOCH TASK
        {
          char buf[30];
          HBPTAG(-,getTaskEpochName(buf,30,(HartTaskIndex) t, (HartEpochIndex) epoch, fAll.mHartNum));
        }
        {
          AtomicScopeLock guard(ps.mLock);          
          ps.mEpochTaskDoneFlags[t][epoch] |= memask; // "I HAVE DONE THIS EPOCH TASK"
          if (result == TEFR_HART_OUT) {
            ps.mHartsOut |= memask;                   // AAAND MY HART'S INITSEQ IS FINISHED
            HBNOTE(HART OUT);
            return;
          }
        }
        HBPTAG(@t,t);
        HBPTAG(@e,epoch);
        HBXX((u32)ps.mEpochTaskDoneFlags[t][epoch]);
      }
      // Special epoch check hb only
      if (me == HARTNUM_B) {
        //HBNOTE(BM0);
        AtomicScopeLock guard(ps.mLock); // HOLD LOCK FOR CHECK & INCR
        //HBNOTE(BM1);
        bool epochDone = true;
        for (u8 t = 0; t < HART_TASK_COUNT; ++t) {
          u8 whomask = hartTaskTable[t][epoch];
          if (whomask != ps.mEpochTaskDoneFlags[t][epoch]) { // done by all who should?
            epochDone = false;
            break;
          }
        }
        if (epochDone) {
          if (ps.mEpochColumn > 0) HBPTAG(END:,getHartEpochName((HartEpochIndex) ps.mEpochColumn));
          ++ps.mEpochColumn;
          if (ps.mEpochColumn < HART_EPOCH_COUNT)
            HBPTAG(START:,getHartEpochName((HartEpochIndex) ps.mEpochColumn));
        }
      }
    }
    //    HBNOTE(<<END INIT SEQ>>);
  }

  HartTaskerPublicState theHartTaskerPublicState;

  void setupHartTaskerInits() {
    theHartTaskerPublicState.init();
  }

  extern HostBlock theHostBlock;

  void runHartTaskerInits() {
    HostBlock & hb = theHostBlock;
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::INITTING; 
    HartTaskerPrivate udaMan;
    udaMan.init(theHartTaskerPublicState);
    udaMan.run();
    HBNOTE(<<EIS>>);
    hb.mPerHartStatus[fAll.mHartNum] = FAILCode::INITTED; 
  }

}
