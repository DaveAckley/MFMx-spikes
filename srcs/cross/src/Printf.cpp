#include "Printf.h"
#include "Fail.h"
#include "AtomicLock.h"
#include "FastLocal.h"
#include "Debug.h"

#if 1
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1  //<<<< setting this drags in s/w div and mod !?
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

  XPrinter DEVNULL;
  AtomicLock DevNullLock; // seriously? must take turns to do nothing?
  static void devnullPrintPutc(int c, void * ctx /*Printer*/) { /* empty */ }

  Printer DP;
  AtomicLock DPLock; // for debug printfing to hostblock

  static void debugPrintPutc(int c, void * ctx /*Printer*/) {
    theHostBlock.addByte((u8) c);
  }

  Printer LOG;
  static void logPrintPutc(int c, void * ctx /*Printer*/) {
    MFM_API_ASSERT_NONNULL(ctx);
    Printer & prt = *(Printer*) ctx;
    FAIL(INCOMPLETE_CODE);
    //    P2PLogElevatorPlatform & mgr = prt.getPlatform();
    //    bool worked = mgr.sendByte(c);
    //    if (!worked) FAIL(OUT_OF_ROOM);
  }


  void t6InitPrinters(HostBlock & hb) {
    u64 hostaddr = hb.getHostNoCAddr();
    //    DP.init(DPLock,0,debugPrintPutc,hostaddr);
    debugPrintPutc('!',0);      // Flag debug initted
    debugPrintPutc('*',0);      // Flag debug initted
    //DP.printf("^");             // test DP.printfing
    debugPrintPutc('?',0);      // Flag debug initted
    //LOG.init(t6et.mP2PLogTransport.getPlatformLock(),&t6et.mP2PLogTransport,logPrintPutc,hostaddr);
    //    DEVNULL.init(DevNullLock,0,devnullPrintPutc,0);
  }

  u32 Printer::vprintf(const char * format, va_list ap) {
    if (!mPutc || !mPrintLockPtr) {
      HBMARK;
      HBNOTE(format);           // a hint before dying?
    }
    MFM_API_ASSERT_NONNULL(mPutc);
    MFM_API_ASSERT_NONNULL(mPrintLockPtr);
    void * contextIsPrinter = this;
    //    ScopeShLock guard(*mPrintLockPtr);
    AtomicScopeLock guard(*mPrintLockPtr);
    u32 ret = npf_vpprintf(mPutc,contextIsPrinter,format,ap);
    return ret;
  }

  s32 snprintf(char * buf, u32 siz, const char * format, ...) {
    va_list ap;
    va_start(ap,format);
    s32 ret = npf_vsnprintf(buf,siz,format,ap);
    va_end(ap);
    return ret;
  }

  u32 Printer::printf(const char * format, ...) {
    va_list ap;
    va_start(ap,format);
    u32 ret;
    /*
    if (false && this == &DP &&
        (fAll.mNoC0.x < 3 || fAll.mNoC0.x > 5 ||
         fAll.mNoC0.y < 3 || fAll.mNoC0.y > 5))
      ret = 1u;
    else
    */
      ret = this->vprintf(format,ap);
    va_end(ap);
    return ret;
  }
}
