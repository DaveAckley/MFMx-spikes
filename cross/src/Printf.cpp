#include "Printf.h"

#if 1
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0  //<<<< setting this drags in s/w div and mod !?
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_ALT_FORM_FLAG 1
#endif
#define NANOPRINTF_IMPLEMENTATION
#include "nanoprintf.h"

#include "HostBlock.h"

namespace MFM {
  extern HostBlock theHostBlock;

  DebugPrint DP;

  static void debugPrintPutc(int c, void * ctx) {
    theHostBlock.addByte((u8) c);
  }

  u32 DebugPrint::printf(const char * format, ...) {
    mLock.acquireLock();
    void * contextToCome = 0;
    va_list ap;
    va_start(ap,format);
    u32 ret = npf_vpprintf(debugPrintPutc,contextToCome,format,ap);
    va_end(ap);
    mLock.releaseLock();
    return ret;
  }
}
