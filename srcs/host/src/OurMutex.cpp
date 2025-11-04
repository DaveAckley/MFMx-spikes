#include "OurMutex.h"
#include "BHLog.h"

namespace MFM {
  void OurMutex::print(FILE * file) {
    if (mThreadLockerId == 0u)
      fprintf(file,"[%s FREE]",mMutexName);
    else {
      BHLog & bhl = BHLog::getTheBHLog();
      fprintf(file,"[%s=>%3d]",mMutexName, bhl.getThrIdOfThreadId(mThreadLockerId));
    }
  }
}

