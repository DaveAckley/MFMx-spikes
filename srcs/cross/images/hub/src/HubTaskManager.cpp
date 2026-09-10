#include "TaskWorker.h"
#include "HubTaskManager.h"
#include "Fail.h"
#include "Debug.h"
#include "FastLocal.h" // for fAll
#include "Grid.h"

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
    
    s8 maybeHandleBolt(PhaserBolt & bolt, u8 lastSeqNo) {
      LOGPTAG(HERBO,lastSeqNo);
      PhaserBolt::Cmd cmd = bolt.getCmd();
      if (cmd == PhaserBolt::CMD_SUSPEND_EWPS) {
        L1GridManagerControl & lgmc = theL1GridManagerControl;
        s32 val = -1;
        bolt.getBoltDataWordIfAny(0,val);
        MFM_API_ASSERT(val >= 0, ILLEGAL_STATE);
        bool reqSuspendEwps = (val!=0);
        LOGPTAG(HERBO_SUSP,reqSuspendEwps);
        if (reqSuspendEwps != lgmc.isEPSuspReq())
          lgmc.setEPSuspReq(reqSuspendEwps);
        else
          LOGPTAG(HERBO_WTF,lgmc.isEPSuspReq());
        // XXX HOW TO HANDLE DELAYED RESPONSE
      } else if (cmd == PhaserBolt::CMD_SUPERCELL_LEADER) {
        L1GridManagerControl & lgmc = theL1GridManagerControl;
        s32 val = -1;
        bolt.getBoltDataWordIfAny(0,val); // new leader number
        MFM_API_ASSERT(val >= 0, ILLEGAL_STATE);
        MFM_API_ASSERT(val <= (s32) U8_MAX, ILLEGAL_STATE);
        lgmc.setSuperCellLeader((u8) val);
        LOGPTAG(HERBO_SUPERCELL,val);
      } else {
        LOGPTAG(HERBO_UNHANDLED,PhaserBolt::phaserCmdName(cmd));
      }
      return 1; //bolt handled; send response
    }

    /// spike for now:
    TaskXFerRB mHN2HB;
    TaskXFerRB mHB2HN;
  };

  HubImageTaskManager theHubTaskManager;

  void TaskWorker::initTaskManagerNC() {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    HBNOTE(TWiTM);
    theHubTaskManager.init();
    HBPX(sizeof(theHubTaskManager));
    if (true) {
      // SPIKE
      u8 taskNumber = theHubTaskManager.createTask(8);
      HBPX(taskNumber);
      Task & task = theHubTaskManager.getTask(taskNumber);
      HBPX(task.mTaskType);
      HBPX(theHubTaskManager.deleteTask(taskNumber));
    }
  }

  void TaskWorker::updateHartTasks() {
    switch (fAll.mHartNum) {
    case HARTNUM_NC:
      EACH(1'000'000,HBPTAG(TWuHTnc,__EACHNUM__));
      {
        s8 ret = theHubTaskManager.checkPhaserDispatch();
        if (ret >= 0)
          EACH(1,LOGPTAG(checkPretNONNEG,ret));
        else
          EACH(1'000'000,LOGPTAG(checkPret,ret));
      }
      break;
    case HARTNUM_B:
      EACH(1'000'000,HBPTAG(TWuHTb,__EACHNUM__));
      break;
    default:
      EACH(1'000'000,HBPTAG(TWuHTdef,__EACHNUM__));
    }
  }

  u8 TaskWorker::createTask(u8 taskType) {
    u8 taskNumber = theHubTaskManager.createTask(taskType);
    HBPX(taskNumber);
    return taskNumber;
  }

  Task& TaskWorker::getTask(u8 taskNumber) {
    return theHubTaskManager.getTask(taskNumber);
  }

  bool TaskWorker::deleteTask(u8 taskNumber) {
    return theHubTaskManager.deleteTask(taskNumber);
  }
}

