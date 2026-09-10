#include "TaskWorker.h"
#include "EwpTaskManager.h"
#include "Fail.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll

namespace MFM {
  struct EwpImageTaskManager : public TaskManager<EwpImageTaskManager> {
    TaskXFerRB * getXFerRBIfAny(u32 hartNumFrom, u32 hartNumTo) {
      return nullptr;
    }

    s8 maybeHandleBolt(PhaserBolt & bolt, u8 lastSeqNo) {
      return 1;                 // not our monkey; just turn it around
    }
  };

  EwpImageTaskManager theEwpTaskManager;

  void TaskWorker::initTaskManagerNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    HBNOTE(TWiTM);
    theEwpTaskManager.init();
  }

  void TaskWorker::updateHartTasks() {
    if (fAll.mHartNum == HARTNUM_NC) {
      {
        s8 ret = theEwpTaskManager.checkPhaserDispatch();
        if (ret < 0)
          EACH(1'000'000,LOGPTAG(checkPret,ret));
        else
          EACH(1,LOGPTAG(checkPretNN,ret));
      }
    }
    EACH(1'000'000,HBPTAG(TWuHT,__EACHNUM__));
  }

  u8 TaskWorker::createTask(u8 taskType) {
    u8 taskNumber = theEwpTaskManager.createTask(taskType);
    HBPX(taskNumber);
    return taskNumber;
  }

  Task& TaskWorker::getTask(u8 taskNumber) {
    return theEwpTaskManager.getTask(taskNumber);
  }

  bool TaskWorker::deleteTask(u8 taskNumber) {
    return theEwpTaskManager.deleteTask(taskNumber);
  }

}
