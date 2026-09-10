#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "RingBuffer.h"
#include "AtomicLock.h"
#include "PHASER.h"

namespace MFM {
  struct Task {
    enum TaskType : u8 {
      TTYPE_NONE = 0,
      TTYPE_IHPPING,
    };
    u8 mTaskNumber;
    u8 mTaskType;
    u8 mBArg1;
    u8 mBArg2;
    u16 mHArg1;
    u16 mHarg2;
    u32 mWArg1;
    u32 mWArg2;
  };

  struct TaskCommon {
    static constexpr u32 MAX_TASKS_PER_XFER = 4;

    static constexpr u8 TASK_NUMBER_NONE = U8_MAX;
    static constexpr u8 TASK_TYPE_FREE = U8_MAX;

    typedef RingBuffer<u8,MAX_TASKS_PER_XFER> TaskXFerRB;

  };

  template<class IMGTM> //< TaskManager subclass per image
  struct TaskManager : public TaskCommon {

    // self(): access this by subtype
    IMGTM& self() { return static_cast<IMGTM&>(*this); }
    IMGTM const & self() const { return static_cast<IMGTM const&>(*this); }

    // "API"
    TaskXFerRB * getXFerRBIfAny(u32 hartNumFrom, u32 hartNumTo) {
      return self().getXFerRBIfAny(hartNumFrom, hartNumTo);
    }

    /*
      \return >0 to respond to bolt and update lastseqno
      \return ==0 to hold and not update lastseqno
      \return <0 to drop bolt without response and update lastseqno
     */
    s8 maybeHandleBolt(PhaserBolt & bolt, u8 lastSeqNo) {
      return self().maybeHandleBolt(bolt,lastSeqNo);
    }

    void updateHartTasks() {
      self().updateHartTasks();
    }

    // SERVICES
    Task * getCurrentTaskPtrIfAny(u8 hartNumber) ;

    TaskXFerRB & getXFerRBOrDie(u32 hartNumFrom, u32 hartNumTo) ;

    bool routeTaskTo(u8 taskNumber, u8 hartNumber) ;
    void holdTaskHere(u8 taskNumber) ;
    void retireTaskDone(u8 taskNumber) ;

    AtomicLock mLock;

    static constexpr u32 MAX_TASKS_IN_USE = 10;
    Task mTaskArray[MAX_TASKS_IN_USE];

    void init() {
      memset_s(this,0,sizeof(*this));
      for (u8 i = 0; i < MAX_TASKS_IN_USE; ++i) {
        mTaskArray[i].mTaskNumber = i;
        mTaskArray[i].mTaskType = TASK_TYPE_FREE;
      }
    }
    
    u8 createTask(u8 taskType) {
      AtomicScopeLock guard(mLock);
      
      MFM_API_ASSERT(taskType != TASK_TYPE_FREE, ILLEGAL_ARGUMENT);
      for (u8 i = 0; i < MAX_TASKS_IN_USE; ++i) {
        Task & t = mTaskArray[i];
        if (t.mTaskType == TASK_TYPE_FREE) {
          t.mTaskType = taskType;
          memoryFence();
          return i;
        }
      }
      return TASK_NUMBER_NONE;
    }

    bool deleteTask(u8 taskNumber) {
      MFM_API_ASSERT(taskNumber < MAX_TASKS_IN_USE, ILLEGAL_ARGUMENT);

      AtomicScopeLock guard(mLock);
      Task & t = mTaskArray[taskNumber];
      if (t.mTaskType == TASK_TYPE_FREE) return false;
      t.mTaskType = TASK_TYPE_FREE;
      return true;
    }

    Task & getTask(u8 taskNumber) {
      MFM_API_ASSERT(taskNumber < MAX_TASKS_IN_USE, ILLEGAL_ARGUMENT);

      AtomicScopeLock guard(mLock);
      Task & t = mTaskArray[taskNumber];
      MFM_API_ASSERT(t.mTaskType != TASK_TYPE_FREE, ILLEGAL_STATE);
      return t;
    }

    //// TaskWorker options
    s8 checkPhaserDispatch() ;
  };
}

#include "TaskManager.tcc"


