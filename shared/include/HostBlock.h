#ifndef HOSTBLOCK_H  /* -*- C++ -*- */
#define HOSTBLOCK_H

#include "itype.h"
#include "RingBuffer.h"

namespace MFM {
  struct HostBlock {
    static const u32 HBMAGIC = 0xACAB8645; // to stay in-theme but not ..47
    static const u32 HBCIGAM = 0x5468BACA; // reverse is easier to see than invert..

    //// DATA MEMBERS
    u32 mHBMagic;               // MUST BE FIRST u32 BYTES 0..3

    s32 mPerHartStatus[5];      // MUST BE 2ND s32(x5) BYTES 4..23
    u8 mXPos, mYPos, mTLBI, mRsrv1; // MUST BE BYTES 24..27
    u32 mCommonArgs[3];

    u32 mHostBaseAddrLo;
    u32 mHostBaseAddrHi;

    typedef RingBuffer<u8,5u> LogBuffer;
    LogBuffer mLogBuffer;

    u32 mHBCigam;         // MUST BE LAST u32

    //// METHODS
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
