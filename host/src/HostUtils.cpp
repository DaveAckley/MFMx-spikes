#include "HostUtils.h"
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "BHLog.h"

namespace MFM {
  void sleepUsec(u32 usec) {
    struct timespec ts;
    const u32 ONE_THOUSAND = 1'000u;
    const u32 ONE_MILLION = ONE_THOUSAND * ONE_THOUSAND;
    ts.tv_sec = usec/ONE_MILLION;
    ts.tv_nsec = (usec%ONE_MILLION)*ONE_THOUSAND;
    nanosleep(&ts,NULL);
  }

  FILE * getHostLog() {
    static bool initted;
    static FILE * logfile;
    if (!initted) {
      struct tm *tmp;
      time_t t = time(NULL);
      tmp = localtime(&t);
      char buf[100];
      strftime(buf,100,"/tmp/MFMx-%Y%m%d-%H%M%S.txt",tmp);
      logfile = fopen(buf,"w+"); // just stomp on existing come on
      initted = true;
    }
    return logfile;
  }
  void EEprintf(const char * file, u32 line, const char * fmt, ...) {
    FILE * logfile = getHostLog();
    char * base = strrchr((char*) file,'/');
    if (base) file = base+1;
    fprintf(logfile,"%s:%d: ",file,line);
    BHLog & bhl = BHLog::getTheBHLog();
    bhl.logLockStates();
    va_list args;
    va_start(args, fmt);
    vfprintf(logfile,fmt,args);
    va_end(args);
    fflush(logfile);
  }
  
}
