#include "HostUtils.h"
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <cxxabi.h>             // for demangle ugh
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
  static std::chrono::time_point<std::chrono::steady_clock> steadyStartTime;
  void initHostClocks() {
    startTime = std::chrono::system_clock::now();
    steadyStartTime = std::chrono::steady_clock::now();
  }

  double runTimeSeconds() {
    TimeStamp now = std::chrono::steady_clock::now();
    typedef std::chrono::duration<double> dsecs;
    dsecs secs = std::chrono::duration_cast<dsecs>(now - steadyStartTime);
    double seconds = secs.count();
    return seconds;
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

  static std::string hostlogdir;
  static FILE * hostlogfile = NULL;

  void initHostLogging() {
    std::string dt = dateTimeStamp();
    hostlogdir = "/tmp/MFMx-" + dt + "/";
    // create dir to hold all the rest.
    std::filesystem::create_directories(hostlogdir+"tiles/");
    std::string logpath = hostlogdir + "all.txt";
    hostlogfile = fopen(logpath.c_str(),"w+"); // just stomp on existing come on
    fprintf(hostlogfile,"pid=%d,tid=%lu\n",
            ::getpid(),
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
  }

  FILE * getHostLog() {
    MFM_API_ASSERT_NONNULL(hostlogfile);
    return hostlogfile;
  }

  FILE * getHostLogForKey(const BHTag & key) { // CALLER MUST CLOSE RETURNED FILE *
    std::string keypath = hostlogdir + "tiles/" + key.to_string() + ".dat";
    FILE * keylog = fopen(keypath.c_str(),"a"); // make then append
    fprintf(keylog,"---%0.4f---\n",runTimeSeconds());
    return keylog;
  }

  void EEvprintf(const char * file, u32 line, const char * fmt, va_list args) {
    FILE * logfile = getHostLog();
    char * base = strrchr((char*) file,'/');
    if (base) file = base+1;
    fprintf(logfile,"%s:%d: ",file,line);
    BHLog & bhl = BHLog::getTheBHLog();
    bhl.logLockStates();
    vfprintf(logfile,fmt,args);
    fflush(logfile);
  }

  void KTEEwrite(const BHTag & key, const u8 * bytes, u32 len) {
    FILE * logfile = getHostLogForKey(key);
    fwrite((const char *) bytes,len,1u,logfile);
    fclose(logfile);
  }

  void KTEEprintf(const BHTag & key, const char * file, u32 line, const char * fmt, ...) {
    // TIMESTAMP?
    char * base = strrchr((char*) file,'/');
    if (base) file = base+1;

    va_list args;
    va_start(args, fmt);
    FILE * logfile = getHostLogForKey(key);
    fprintf(logfile,"%s:%d: ",file,line);
    vfprintf(logfile,fmt,args);
    va_end(args);
    fclose(logfile);
  }

  void EEprintf(const char * file, u32 line, const char * fmt, ...) {
    FILE * logfile = getHostLog();
    char * base = strrchr((char*) file,'/');
    if (base) file = base+1;
    fprintf(logfile,"%s:%d: ",file,line);
    //BHLog & bhl = BHLog::getTheBHLog();
    //bhl.logLockStates();
    va_list args;
    va_start(args, fmt);
    vfprintf(logfile,fmt,args);
    va_end(args);
    fflush(logfile);
  }

  void interpretFailBits(u8 failbits, u8 * data, u32 count) {
    const char chars[] = "BTT0T1T2NC";
    u32 next = 0u;
    for (u32 h = 0u; h<5u; ++h) {
      if (failbits & (1<<h)) {
        if (next < count-1) data[next++] = chars[2*h];
        if (next < count-1) data[next++] = chars[2*h+1];
      }
    }
    if (next == 0u && next < count-1)
      data[next++] = '+'; // all good
    if (next < count)
      data[next] = 0u;
  }
  
  std::string pct4(u64 num, u64 den) {
    if (den == 0u) return "---%";
    constexpr u64 oneC= 100u;
    constexpr u64 tenK = oneC*oneC;
    u64 tKrat = tenK * num / den;
    // [0 .. )

    if (tKrat >= 10'000u*oneC) return "+++%";

    if (tKrat >= 100u*oneC)
      return std::to_string(tKrat/oneC) + "%"; // '999%'

    // else [0 .. 10,000)
    if (tKrat >= 10u*oneC)
      return std::string(" ")
        + std::to_string(tKrat/oneC) + "%"; // ' 99%'

    // else [0 .. 1,000)      
    if (tKrat >= 1u*oneC) 
      return std::to_string(tKrat/oneC) + "."
        + std::to_string(tKrat/oneC%10u) + "%";

    // else [0 .. 100)
    if (tKrat > 10u)
      return std::string(".")
        + std::to_string((tKrat/10u) % 10u)
        + std::to_string(tKrat % 10u) + "%";

    // else [0 .. 10)
    return "0%";   
  }

  std::string toHex(u64 num) {
    std::string s(2*sizeof(u64),'.');
    const auto res = std::to_chars(s.data(), s.data() + s.size(), num, 16);
    return s.substr(0,res.ptr - s.data());
  }

  std::string size4(u64 num) {
    std::string ret;
    const u32 scaler = 1000;
    const char suffixes[] = ".KMGTQ";
    if (num < 10*scaler) {
      u32 n = scaler;
      while (n > 1u && num < n) {
        ret += " ";
        n /= 10u;
      }
      ret += std::to_string(num);        
      return ret;
    }
    for (u32 i = 0; i < sizeof(suffixes); ++i) {
      if (num < scaler) {
        if (num < scaler/100u) ret += " ";
        if (num < scaler/10u) ret += " ";
        ret += std::to_string(num) + suffixes[i];
        break;
      }
      if (num < 10*scaler && i < sizeof(suffixes) - 1) {
        u32 unum = (u32) num;
        u32 intpart = unum / scaler;
        u32 fracpart = (unum - intpart * scaler) / 100;
        ret += std::to_string(intpart);
        ret += ".";
        ret += std::to_string(fracpart);
        ret += suffixes[i + 1];
        break;
      }
      num /= scaler;
    }
    return ret;
  }

  std::string demangleCpp(const char* typeName) {
    int status = 1;
    std::unique_ptr<char, void(*)(void*)> res {
      abi::__cxa_demangle(typeName, NULL, NULL, &status),
      std::free
    };
    return (status==0) ? res.get() : typeName ;
  }
  
  void initHostUtils() {
    static bool initted = false;
    MFM_API_ASSERT_STATE(!initted); // rumemba: one ping only.
    initHostClocks();
    initHostLogging();
    initted = true;
  }

}
