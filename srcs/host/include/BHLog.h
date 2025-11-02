#pragma once /* -*- C++ -*- */
#include <sstream>
#include "itype.h"
#include "BHTag.h"
#include "Fail.h"
#include "HostUtils.h"
#include "OurMutex.h"

#include <pybind11/pybind11.h>
namespace py = pybind11;

namespace MFM {

  inline const char * myGILState() { return PyGILState_Check() ? "MYGIL" : "nogil"; }

  using LogCallback = std::function<void(const BHTag &,const std::string_view&)>;

  enum BHLogDest : u8 {
    DISCARD,    // 0
    STDOUT,     // 1
    STDERR,     // 2
    FAIL        // 3
  };
  
  struct BHLog {

    static BHLog & getTheBHLog() __attribute__ ((used)); // SINGLETON at least for now

    void logLockStates() ;

    OurMutex mPythonCallbackMutex;
    LogCallback mLogCallback;
    BHLogDest mDefaultLogDest;
    u32 mBaseThreadId;
    std::atomic<u32> mLastThreadId;

    void captureBaseThreadIdOnce() {
      if (mBaseThreadId == 0u)
        mBaseThreadId = gettid();
    }
    s32 getThrId() { return getThrIdOfThreadId(gettid()); }
    s32 getThrIdOfThreadId(u32 tid) {
      u32 last = mLastThreadId.load();
      if (last != tid) {
        mLastThreadId.store(tid);

        FILE * f = getHostLog();
        fprintf(f,"  [%u ==>> %u]\n",
                last - (s32) mBaseThreadId + 100u,
                tid - (s32) mBaseThreadId + 100u); 
      }
      return tid - (s32) mBaseThreadId + 100u;  // offset 100 for 'recognizability'
    }

    BHLog()
      : mPythonCallbackMutex("PYCB")
      , mLogCallback(0)
      , mDefaultLogDest(BHLogDest::STDOUT)
      , mBaseThreadId(0)
    { }

    inline bool wantsTag(const BHTag tag) {
      // NO LOCK?? std::lock_guard<std::mutex> lock(mPythonCallbackMutex); // get mutex to access mLogCallback
      return
        mLogCallback != 0 ||
        mDefaultLogDest != BHLogDest::DISCARD;
    }

    void printf(const BHTag tag, const char * fmt, ...) {
      va_list args;
      va_start(args, fmt);
      BHLog::getTheBHLog().vprintf(tag,fmt,args);
      va_end(args);
    }

    void vprintf(const BHTag tag, const char * fmt, va_list va) ;

    void handle(const BHTag tag, const u8 * data, u32 bytecount) ;

    bool setLogCallbackNoGIL(LogCallback cb) {
      Eprintf("setLogCallbackNoGIL (%u) 10 : %d\n",getThrId(),cb != 0);
      // Acquire C++ mutex to protect mLogCallback
      //      std::lock_guard<std::mutex> lock(mPythonCallbackMutex);
      OurScopeLock guard(mPythonCallbackMutex);
      Eprintf("setLogCallbackNoGIL (%u) 11\n",getThrId());
      mLogCallback = cb;
      Eprintf("setLogCallbackNoGIL (%u) 12 : %d \n",getThrId(),mLogCallback != 0);
      return mLogCallback != 0;
    }

    bool setDefaultDestination(u32 dest) {
      if (dest >= BHLogDest::DISCARD && dest <= BHLogDest::FAIL) {
        mDefaultLogDest = (BHLogDest) dest;
        return true;
      }
      return false;
    }

    std::string to_repr() {
      std::stringstream ss;
      //      std::lock_guard<std::mutex> lock(mPythonCallbackMutex); // get mutex to access mLogCallback
      ss << "<BHLog:callback="
         << (mLogCallback?"set":"cleared")
         << ",default=" << ((u32) mDefaultLogDest)
         << ",at=0x" << std::hex << ((uintptr_t) this)
         << ">" ;
      return ss.str();
    }

    static void pybindings(py::module & m) {
      getTheBHLog().captureBaseThreadIdOnce();
      
      py::class_<BHLog> lg(m,"BHLog");
      lg.def_static("getLog",&BHLog::getTheBHLog, py::return_value_policy::reference);
      lg.def("setDefaultDestination",&BHLog::setDefaultDestination,py::call_guard<py::gil_scoped_release>());
      //lg.def("__str__",&BHLog::to_string);
      lg.def("__repr__",&BHLog::to_repr,py::call_guard<py::gil_scoped_release>());

      lg.def("clearLogCallback", [](BHLog& bhl) { bhl.setLogCallbackNoGIL(0); },py::call_guard<py::gil_scoped_release>());
      lg.def("setLogCallback", &BHLog::setLogCallbackNoGIL,py::call_guard<py::gil_scoped_release>());
    }
  };

  inline void LOGprintf(u32 bhcard, const char * fmt, ...) {
    Eprintf("LOGprintf in '%s'\n",fmt);
    BHTag tag(TagType::APPDBG,bhcard,0,0);
    va_list args;
    va_start(args, fmt);
    BHLog::getTheBHLog().vprintf(tag,fmt,args);
    va_end(args);
    Eprintf("LOGprintf out '%s'\n",fmt);
  }
  
}

