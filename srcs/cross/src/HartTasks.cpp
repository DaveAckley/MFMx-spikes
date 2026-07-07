#include "HartTasks.h"
#include "CrossUtils.h"
#include "FastLocal.h"          // for fAll
#include "Debug.h"
#include "Printf.h"             // for snprintf

namespace MFM {

#define XX(N) ,"" #N
  const char *hartEpochNames[HART_EPOCH_COUNT] = {
    "illegal"
    ALL_HART_EPOCHS
  };
#undef XX

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
#define XX(BE,BI,YO,LI,DI,N)                                            \
  TEFResult __attribute__((weak)) TaskEpochFunction_##N (               \
                   HartTaskIndex hti, HartEpochIndex hei, u8 hartnum) { \
    TEFDie(hti,hei,hartnum);                                            \
    return TEFR_HART_OUT; /* NOT REACHED */                             \
  }
  ALL_HART_TASKS
#undef XX

  /// Generate table of function ptrs
  const TaskEpochFuncPtr hartTaskEpochFunctionPtrs[HART_TASK_COUNT] = { 
    0 // illegal task
#define XX(BE,BI,YO,LI,DI,N) ,&TaskEpochFunction_##N
    ALL_HART_TASKS
#undef XX
  };

  /// Generate table of task names
#define XX(BE,BI,YO,LI,DI,N) ,"" #N
  const char *hartTaskNames[HART_TASK_COUNT] = {
    "illegal"
    ALL_HART_TASKS
  };
#undef XX

  /// Generate the main task table
  // generate first row 
#define XX(N) ,HM__
  const u8 hartTaskTable[HART_TASK_COUNT][HART_EPOCH_COUNT] = {
    { HM__ ALL_HART_EPOCHS } // blow 0th row
#undef XX
    
  // generate rest of rows
#define XX(BE,BI,YO,LI,DI,N) \
    ,{HM__, /*blow col0*/    \
      HM_##BE,               \
      HM_##BI,               \
      HM_##YO,               \
      HM_##LI,               \
      HM_##DI }
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
        if (0==(memask & whomask)) continue; // not my monkey
        if (0!=(memask & ps.mEpochTaskDoneFlags[t][epoch])) continue; // already done
        HBASSERT_EQ((whomask & ps.mHartsOut),0); // ded if we need any out monkeys

        // IT'S TIME FOR ME TO DO THIS EPOCH TASK
        {
          char buf[30];
          HBPTAG(:,getTaskEpochName(buf,30,(HartTaskIndex) t, (HartEpochIndex) epoch, fAll.mHartNum));
        }
        TEFResult result = (*hartTaskEpochFunctionPtrs[t])((HartTaskIndex) t, (HartEpochIndex) epoch, fAll.mHartNum);
        {
          AtomicScopeLock guard(ps.mLock);          
          ps.mEpochTaskDoneFlags[t][epoch] |= memask; // "I HAVE DONE THIS EPOCH TASK"
          if (result == TEFR_HART_OUT) {
            ps.mHartsOut |= memask;                   // AAAND MY HART'S INITSEQ IS FINISHED
            HBNOTE(HART OUT);
            return;
          }
        } 
      }
      // Special epoch check hb only
      if (me == HARTNUM_B) {
        AtomicScopeLock guard(ps.mLock); // HOLD LOCK FOR CHECK & INCR
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

}
