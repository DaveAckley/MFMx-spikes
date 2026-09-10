#pragma once   /* -*- C++ -*- */

#include "TaskManager.h"

namespace MFM {

  namespace TaskWorker {
    void initTaskManager(); //< one time init to be run by hart nc only
    void updateHartTasks(); //< defined in image/../ImageTaskManager
    u8 createTask(u8 taskType); //< "
    Task& getTask(u8 taskNumber); //< "
  }
}
