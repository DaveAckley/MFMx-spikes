#include "TaskWorker.h"
#include "EwpTaskManager.h"
#include "Fail.h"
#include "Debug.h"

namespace MFM {
  struct EwpImageTaskManager : public TaskManager<EwpImageTaskManager> {
    TaskXFerRB * getXFerRBIfAny(u32 hartNumFrom, u32 hartNumTo) {
      return nullptr;
    }
  };

  EwpImageTaskManager theTaskManager;

  void TaskWorker::initTaskManager() {
    HBNOTE(TWiTM);
    theTaskManager.init();
  }

  void TaskWorker::updateHartTasks() {
    if (fAll.mHartNum == HARTNUM_NC) {
      
    }
    EACH(1'000'000,HBPTAG(TWuHT,__EACHNUM__));
  }

}
