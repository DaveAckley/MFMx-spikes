#include "TaskWorker.h"
#include "HubTaskManager.h"
#include "Fail.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  struct HubImageTaskManager : public TaskManager<HubImageTaskManager> {
    TaskXFerRB * getXFerRBIfAny(u32 hartNumFrom, u32 hartNumTo) {
      // SPIKE
      if (hartNumFrom == HARTNUM_NC && hartNumTo == HARTNUM_B)
        return &mHN2HB;
      if (hartNumFrom == HARTNUM_B && hartNumTo == HARTNUM_NC)
        return &mHB2HN;
      return nullptr;
    }
    
    /// spike for now:
    TaskXFerRB mHN2HB;
    TaskXFerRB mHB2HN;
  };

  HubImageTaskManager theTaskManager;

  void TaskWorker::initTaskManager() {
    HBNOTE(TWiTM);
    theTaskManager.init();
    HBPX(sizeof(theTaskManager));
    if (true) {
      // SPIKE
      u8 taskNumber = theTaskManager.createTask(8);
      HBPX(taskNumber);
      Task & task = theTaskManager.getTask(taskNumber);
      HBPX(task.mTaskType);
      HBPX(theTaskManager.deleteTask(taskNumber));
    }
  }

  void TaskWorker::updateHartTasks() {
    switch (fAll.mHartNum) {
    case HARTNUM_NC:
      EACH(1'000'000,HBPTAG(TWuHTnc,__EACHNUM__));
      break;
    case HARTNUM_B:
      EACH(1'000'000,HBPTAG(TWuHTb,__EACHNUM__));
      break;
    default:
      EACH(1'000'000,HBPTAG(TWuHTdef,__EACHNUM__));
    }
  }

  u8 TaskWorker::createTask(u8 taskType) {
    u8 taskNumber = theTaskManager.createTask(taskType);
    HBPX(taskNumber);
    return taskNumber;
  }

  Task& TaskWorker::getTask(u8 taskNumber) {
    return theTaskManager.getTask(taskNumber);
  }
}

