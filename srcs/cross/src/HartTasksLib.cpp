#include "HartTasksLib.h"
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

}
