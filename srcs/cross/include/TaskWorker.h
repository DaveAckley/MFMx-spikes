#pragma once   /* -*- C++ -*- */

#include "itype.h"

namespace MFM {
  struct Task;              // FORWARD

  namespace TaskWorker {
    void initTaskManagerNC(); //< one time init to be run by hart nc only
    void updateHartTasks();   //< defined in image/../ImageTaskManager
    u8 createTask(u8 taskType);   //< "
    Task& getTask(u8 taskNumber); //< "
    bool deleteTask(u8 taskNumber); //< "
  }
}
