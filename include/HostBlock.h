#ifndef HOSTBLOCK_H  /* -*- C++ -*- */
#define HOSTBLOCK_H

#include "RingBuffer.h"

namespace MFM {
  struct HostBlock {
    static const uint32_t HBMAGIC = 0xACAB8645; // to stay in-theme but not ..47
    static const uint32_t HBCIGAM = 0x5468BACA; // reverse is easier to see than invert..

    uint32_t mHBMagic;          // MUST BE FIRST u32

    uint32_t mPerRiscArg[5];    // MUST BE 2ND u32(x5)
    uint32_t mCommonArgs[3];

    uint32_t mHostBaseAddrLo;
    uint32_t mHostBaseAddrHi;

    uint8_t mXPos, mYPos, mRsrv1, mRsrv2;

    typedef RingBuffer<u8,9u> LogBuffer;
    LogBuffer mLogBuffer;

    uint32_t mHBCigam;         // MUST BE LAST u32

    void resetLog() { mLogBuffer.reset(); }

    bool addByte(u8 byte) { return mLogBuffer.add(byte); }

    bool addString(const char * st) {
      u8 byte;
      do { } while ((byte = *st++) && mLogBuffer.add(byte));
      return byte!=0u;
    }

    s32 removeByte() {
      u8 ch;
      if (mLogBuffer.remove(ch)) return (s32) ch;
      return -1;
    }
  };
}

#endif /*HOSTBLOCK_H*/
