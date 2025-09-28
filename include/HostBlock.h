#ifndef HOSTBLOCK_H  /* -*- C++ -*- */
#define HOSTBLOCK_H

#include "RingBuffer.h"

namespace MFM {
  struct HostBlock {
    uint32_t mPerRiscArg[5];
    uint32_t mCommonArgs[3];

    typedef RingBuffer<u8,9u> LogBuffer;
    LogBuffer mLogBuffer;

    void resetLog() volatile { mLogBuffer.reset(); }

    bool addByte(u8 byte) volatile { return mLogBuffer.add(byte); }

    bool addString(const char * st) volatile {
      u8 byte;
      do { } while ((byte = *st++) && mLogBuffer.add(byte));
      return byte!=0u;
    }

    s32 removeByte() volatile {
      u8 ch;
      if (mLogBuffer.remove(ch)) return (s32) ch;
      return -1;
    }
  };
}

#endif /*HOSTBLOCK_H*/
