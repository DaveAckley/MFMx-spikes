#include "BHLog.h"

namespace MFM {
  static BHLog theBHLog;
  BHLog & BHLog::getTheBHLog() { return theBHLog; }

  void BHLog::handle(const BHTag tag, const u8 * data, u32 bytecount) {
    Eprintf("handle10 (%u) %u IN %s\n",getThrId(),bytecount,myGILState());

    // Acquire C++ mutex to protect mLogCallback
    {
      Eprintf("handle11 (%u) %u PRE lock(pycb) %s\n",getThrId(),bytecount,myGILState());
      //      std::lock_guard<std::mutex> lock(mPythonCallbackMutex);
      OurScopeLock guard(mPythonCallbackMutex);
      Eprintf("handle11a (%u) %u POST lock(pycb) %s\n",getThrId(),bytecount,myGILState());
      if (mLogCallback != 0) {
        Eprintf("handle12 (%u) %u PRE stringview %s\n",getThrId(), bytecount,myGILState());
        std::string_view s((const char *) data,bytecount);
        Eprintf("handle12a (%u) PRE GIL (%s) <%s>\n",getThrId(),myGILState(),
                std::string(s).c_str());

        //py::gil_scoped_acquire getGILLY;

        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure(); // Acquire GIL to call back into python

        Eprintf("handle13 (%u) PRE logcb (%s) (%p/%p) (have GIL & pycb) %s\n",
                getThrId(),
                tag.to_string().c_str(),
                data,&s,
                myGILState());
        mLogCallback(tag,s);
        Eprintf("handle14 (%u) POST logcb (%p) (about to release GIL & pycb) %s\n",getThrId(),&mLogCallback,myGILState());

        PyGILState_Release(gstate);
        return;                   // done (releasing GIL and mutex)
      }
    }
    Eprintf("handle15 %u NO LOGCB\n",bytecount);

    if (mDefaultLogDest == BHLogDest::DISCARD) return;
    if (mDefaultLogDest == BHLogDest::FAIL)
      FAIL(USER_REQUESTED_FAILURE);

    fwrite(data, sizeof(char), bytecount,
           (mDefaultLogDest == BHLogDest::STDOUT) ? stdout : stderr);
  }  

  void BHLog::vprintf(BHTag tag, const char * fmt, va_list va) {
    Eprintf("vprintf10 (%u) %s IN\n",getThrId(),fmt);

    if (!wantsTag(tag)) return;
    Eprintf("vprintf11 (%u) POST wantsTag\n",getThrId());
    const u32 BUF_SIZ = 1024u;
    char buf[BUF_SIZ];
    int n = snprintf(buf, BUF_SIZ, "(%d)", getThrId());
    n += vsnprintf(buf+n, BUF_SIZ-n, fmt, va);
    if (n > BUF_SIZ) n = BUF_SIZ;
    Eprintf("vprintf12 (%u) PRE handle {%s}\n",getThrId(),buf);
    handle(tag,(const u8*) buf,n);
    Eprintf("vprintf13 (%u) POST handle OUT {%s}\n",getThrId(),buf);
  }

  void BHLog::logLockStates() {
    FILE * tofile = getHostLog();
    const char * pytstatus = myGILState();
    BHLog & bhl = BHLog::getTheBHLog();
    OurMutex & om = bhl.mPythonCallbackMutex;
    const char * pcb = om.mMutexName;
    u32 pcbwho = om.mThreadLocker;

    if (pcbwho==0u)
      fprintf(tofile,"[LLS:%d %s -pycb] ",bhl.getThrId(),pytstatus);
    else
      fprintf(tofile,"[LLS:%d %s +pycb=%d] ",bhl.getThrId(),pytstatus,bhl.getThrIdOfThreadId(pcbwho));
  }
}
