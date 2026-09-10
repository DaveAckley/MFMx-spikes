#ifndef HOSTBLOCK_H  /* -*- C++ -*- */
#define HOSTBLOCK_H

#include "itype.h"
#include "RingBuffer.h"
#include "UxC.h" // for U8C
#include "SimConstants.h" // for HOST_RAM_PER_BH

namespace MFM {
  static constexpr u8 HOST_COMMS_MAP_CHUNK_SIZE = 64u;

  struct HostBlock {
    static const u32 HBMAGIC = 0xACAB8645; // to stay in-theme but not ..47
    static const u32 HBCIGAM = 0x5468BACA; // reverse is easier to see than invert..

    enum HostFlags : u8 {
      HBF_ASSERT_STOP_LOG = 0x01, // block further hblogging if any HBASSERT blows
      HBF_RSV1 = 0x02,
      HBF_RSV2 = 0x04,
      HBF_RSV3 = 0x08,
      HBF_RSV4 = 0x10,
      HBF_RSV5 = 0x20,
      HBF_RSV6 = 0x40,
      HBF_RSV7 = 0x80
    };

    //// DATA MEMBERS
    u32 mHBMagic;               // MUST BE FIRST u32 BYTES 0..3

    s32 mPerHartStatus[5];      // MUST BE 2ND s32(x5) BYTES 4..23
    U8C mNoC0;                  // MUST BE BYTES 24..25
    u8 mTLBI, mFails;           // MUST BE BYTES 26..27
    u8 mChipNum, mHostFlags, RSV2, RSV3; // MUST BE BYTES 28..31
    u32 mCommonArgs[2];         // MUST BE BYTES 32..39

    u32 mPerHartWatchdog[5];    // spin counter for liveness checks
    u32 mHostBaseAddrLo;        // Lo address of host EPs
    u32 mHostBaseAddrHi;        // Hi address of host EPs

    u32 mAIClockFrequency;

    u32 mAtFailRegSP;           // copies of select regs, only
    u32 mAtFailRegRA;           // valid when failed, intended 
    u32 mAtFailRegFP;           // for stack dump purposes

    typedef RingBuffer<u16,13u> LogBuffer; //XXX waay big shrink to <=10u?
    //typedef RingBuffer<u16,10u> LogBuffer; //stick around here for prod?
    LogBuffer mLogBuffer;

    u16 mPerHartFailFileID[5];  
    u16 mPerHartFailFileLine[5];

    u32 mHBCigam;         // MUST BE LAST u32

    //// METHODS
    bool goodMagic() const {
      return mHBMagic == HBMAGIC && mHBCigam == HBCIGAM; 
    }
    inline void hartbeat(u32 hartnum) {
      if (hartnum < 5) ++mPerHartWatchdog[hartnum];
    }

    u64 getOurHostNoCBaseAddress() const {
      return getHostNoCAddr() + mTLBI * HOST_RAM_PER_BH;
    }

    void resetLog() { mLogBuffer.reset(); }

    bool addU16(u16 ch) { return mLogBuffer.add(ch); }

    bool addBytes(u8 b1, u8 b2) { return addU16((((u16)b2)<<8)|b1); }

    bool addByte(u8 byte) { return addBytes(' ',byte); }

    bool addString(const char * st) {
      u8 byte;
      do { } while ((byte = *st++) && addByte(byte));
      return byte!=0u;
    }

    bool packString(const char *st) {
      u8 b1 = 0, b2 = 0;
      while ((b1 = *st)) {
        ++st;
        if ((b2 = *st)) ++st;
        else b2 = '_';
        addBytes(b1,b2);
        b1 = b2 = 0;
      }
      return true;
    }

    bool removeBytes(u8 & b1, u8 &b2) {
      u16 ch;
      if (mLogBuffer.remove(ch)) {
        b1 = (ch>>8);
        b2 = ch&0xff;
        return true;
      }
      return false;
    }
    s32 removeByte() {
      u16 ch;
      if (mLogBuffer.remove(ch)) return (s32) ch;
      return -1;
    }

  private:
    u64 getHostNoCAddr() const {
      return (((u64) mHostBaseAddrHi)<<32) + mHostBaseAddrLo;
    }

  };
}

#endif /*HOSTBLOCK_H*/
