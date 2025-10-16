#include "Printf.h"
#include "Fail.h"
#include "T6ElevatorTransport.h"

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

  Printer DP;
  static void debugPrintPutc(int c, void * ctx) {
    theHostBlock.addByte((u8) c);
  }

  Printer LOG;
  static void logPrintPutc(int c, void * ctx) {
    MFM_API_ASSERT_NONNULL(ctx);
    T6ElevatorTransport::P2PLogCarManager
      & mgr = theT6ElevatorTransport.mP2PLogCarManager;
    LogCarStorage::LogCar * lcp = mgr.getCurrentCarIfAny();
    if (!lcp) FAIL(INCOMPLETE_CODE);
    if (lcp->getCarState() != CarState::OPEN) FAIL(ILLEGAL_STATE);
    u64 remoteaddr = mgr.getCarRemoteAddress();
    LogBlock & lb = lcp->getContent();
    if (lb.spaceRemaining() == 1u) {
      Printer * prt = (Printer*) ctx;
      lb.addByte('X');
    }
    else lb.addByte(c);
  }

  void t6InitPrinters(HostBlock & hb) {
    u64 hostaddr = hb.getHostNocAddr();
    DP.init(debugPrintPutc,hostaddr);
    LOG.init(logPrintPutc,hostaddr);
  }

  u32 Printer::printf(const char * format, ...) {
    MFM_API_ASSERT_NONNULL(mPutc);
    mLock.acquireLock();
    void * contextToCome = this;
    va_list ap;
    va_start(ap,format);
    u32 ret = npf_vpprintf(mPutc,this,format,ap);
    va_end(ap);
    mLock.releaseLock();
    return ret;
  }
}
