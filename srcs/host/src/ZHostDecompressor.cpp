#include "ZHostDecompressor.h"
#include "HostUtils.h"

namespace MFM {
  static s32 ZHByteSource(bool canread, void * ctxt) { // >=0 byte, -1 eof, -2 blocked
    MFM_API_ASSERT_NONNULL(ctxt);
    ZHostDecompressor & zhd = *(ZHostDecompressor*) ctxt;
    HTprintf("AT ZBSource %p\n",ctxt);
    return -2;
  }
  static bool ZHByteSink(const u8, void * ctxt) { // true: wrote, false: blocked, error, or eof
    MFM_API_ASSERT_NONNULL(ctxt);
    ZHostDecompressor & zhd = *(ZHostDecompressor*) ctxt;
    HTprintf("AT ZBSink %p\n",ctxt);
    return false;
  }

  ZHostDecompressor::ZHostDecompressor()
  {
    HTprintf("ZHD %p HERE HALLO\n",this);
  }

  void ZHostDecompressor::init() {
    mLZ.init(ZHByteSource, this, ZHByteSink, this);
  }
}
