#include "BHLog.h"

namespace MFM {
  static BHLog theBHLog;
  BHLog & BHLog::getTheBHLog() { return theBHLog; }

  void BHLog::handle(const BHTag tag, const u8 * data, u32 bytecount) {
    if (mLogCallback != 0) {
      std::string_view s((const char *) data,bytecount);
      mLogCallback(tag,s);
      return;
    }
    if (mDefaultLogDest == BHLogDest::DISCARD) return;
    if (mDefaultLogDest == BHLogDest::FAIL)
      FAIL(USER_REQUESTED_FAILURE);

    fwrite(data, sizeof(char), bytecount,
           (mDefaultLogDest == BHLogDest::STDOUT) ? stdout : stderr);
  }  

  void BHLog::vprintf(BHTag tag, const char * fmt, va_list va) {
    if (!wantsTag(tag)) return;
    const u32 BUF_SIZ = 1024u;
    char buf[BUF_SIZ];
    int n = vsnprintf(buf, BUF_SIZ, fmt, va);
    if (n > BUF_SIZ) n = BUF_SIZ;
    handle(tag,(const u8*) buf,n);
  }
}
