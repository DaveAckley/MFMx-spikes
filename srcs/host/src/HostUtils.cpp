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

  static std::chrono::time_point<std::chrono::system_clock> startTime;
  void initHostClock() {
    startTime = std::chrono::system_clock::now();
  }

  std::string dateTimeStamp() {
    char buf[100];
    std::time_t time = std::time({});
    std::strftime(buf,100, "%Y%m%d-%H%M%S", std::localtime(&time));
    return std::string(buf);
  }

  u32 millisElapsed() {
    auto now = std::chrono::system_clock::now();
    u32 millis = (now - startTime) / std::chrono::milliseconds(1);
    return millis;
  }

  FILE * getHostLog() {
    static bool initted;
    static FILE * logfile;
    if (!initted) {
      std::string dt = dateTimeStamp();
      std::string path = "/tmp/MFMx-" + dt + ".txt";
      logfile = fopen(path.c_str(),"w+"); // just stomp on existing come on
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
