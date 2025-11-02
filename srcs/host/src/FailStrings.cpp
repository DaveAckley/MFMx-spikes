#include "FailStrings.h"

namespace MFM {
  const char * getFailCodeString(FAILCode fc) {
    switch (fc) {
    default: {
      static char buf[100];
      snprintf(buf,100,"Unknown FAILCode %d?",fc);
      return buf;
    }
#define XX(name) case name: return #name;
#include "FailCodes.h"
#undef XX
    }
  }
}
