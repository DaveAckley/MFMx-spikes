#pragma once /* -*- C++ -*- */
#include <sstream>
#include "itype.h"
#include "BHTag.h"
#include "Fail.h"

#include <pybind11/pybind11.h>
namespace py = pybind11;

namespace MFM {

  using LogCallback = std::function<void(const BHTag &,const std::string_view&)>;

  enum BHLogDest : u8 {
    DISCARD,
    STDOUT,
    STDERR,
    FAIL
  };
  
  struct BHLog {
    static BHLog & getTheBHLog() __attribute__ ((used)); // SINGLETON at least for now

    LogCallback mLogCallback;
    BHLogDest mDefaultLogDest;

    BHLog()
      : mLogCallback(0)
      , mDefaultLogDest(BHLogDest::STDOUT)
    { }

    inline bool wantsTag(const BHTag tag) {
      return
        mLogCallback != 0 ||
        mDefaultLogDest != BHLogDest::DISCARD;
    }

    void vprintf(const BHTag tag, const char * fmt, va_list va) ;

    void handle(const BHTag tag, const u8 * data, u32 bytecount) ;

    bool setLogCallback(LogCallback cb) {
      mLogCallback = cb;
      return mLogCallback != 0;
    }

    bool setDefaultDestination(u32 dest) {
      if (dest >= BHLogDest::DISCARD && dest <= BHLogDest::FAIL) {
        mDefaultLogDest = (BHLogDest) dest;
        return true;
      }
      return false;
    }

    std::string to_repr() const {
      std::stringstream ss;
      ss << "<BHLog:callback="
         << (mLogCallback?"set":"cleared")
         << ",default=" << ((u32) mDefaultLogDest)
         << ",at=0x" << std::hex << ((uintptr_t) this)
         << ">" ;
      return ss.str();
    }

    static void pybindings(py::module & m) {
      py::class_<BHLog> lg(m,"BHLog");
      lg.def_static("getLog",&BHLog::getTheBHLog, py::return_value_policy::reference);
      lg.def("setDefaultDestination",&BHLog::setDefaultDestination);
      //lg.def("__str__",&BHLog::to_string);
      lg.def("__repr__",&BHLog::to_repr);

      lg.def("clearLogCallback", [](BHLog& bhl) { bhl.setLogCallback(0); return bhl; });
      lg.def("setLogCallback", [](BHLog& bhl, py::function cb) {
        bhl.setLogCallback([cb](const BHTag& tag, const std::string_view& chunk) {
          //py::gil_scoped_acquire snatchLock;
          PyGILState_STATE gstate;
          gstate = PyGILState_Ensure();
          try {
            cb(tag,chunk);
          } catch (const py::error_already_set& e) {
            PyErr_Print();
            // re-raise error? or something else?
            //throw;
          }
          PyGILState_Release(gstate);
        });
        return bhl;
      });
    }
  };

  inline void LOGprintf(u32 bhcard, const char * fmt, ...) {
    BHTag tag(TagType::APPDBG,bhcard,0,0);
    va_list args;
    va_start(args, fmt);
    return BHLog::getTheBHLog().vprintf(tag,fmt,args);
    va_end(args);
  }
  
}

