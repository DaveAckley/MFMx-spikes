#ifndef HOSTBLOCK_H  /* -*- C++ -*- */
#define HOSTBLOCK_H

#include "itype.h"
#include "HostFlags.h"
#include "RingBuffer.h"
#include "UxC.h" // for U8C
#include "SimConstants.h" // for HOST_RAM_PER_BH

namespace MFM {
  static constexpr u8 HOST_COMMS_MAP_CHUNK_SIZE = 64u;

  struct HostBlock {
    static const u32 HBMAGIC = 0xACAB8645; // to stay in-theme but not ..47
    static const u32 HBCIGAM = 0x5468BACA; // reverse is easier to see than invert..

    //// DATA MEMBERS
    u32 mHBMagic;               // MUST BE FIRST u32 BYTES 0..3

    s32 mPerHartStatus[5];      // MUST BE 2ND s32(x5) BYTES 4..23
    U8C mNoC0;                  // MUST BE BYTES 24..25
    u8 mTLBI, mFails;           // MUST BE BYTES 26..27
    u8 mChipNum, mHostFlags;    // MUST BE BYTES 28..29

    /* Eth-seam live-link masks, 4 bits per neighbor-card Dir4 (N=0,
       W=1, S=2, E=3), written by the host at deploy (all zero = no
       seams).  Bit i of a nibble says the i-th link of the cabling
       list for that direction is live.  The lists, and what a cable
       change means, are documented at EthSeam::SEAM_LINKS_EW/NS in
       shared/EthSeam.h.  Use the accessors below.
       MUST BE BYTES 30..31 (these were RSV2, RSV3: layout and size
       are unchanged) */
    u8 mEthSeamLo, mEthSeamHi;
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

    /** The four link nibbles as one 16-bit word: Dir4 d is at bits
        4d..4d+3.  See EthSeam::decodeSeamMask. */
    u16 getEthSeamMask() const {
      return (u16) (mEthSeamLo | (mEthSeamHi << 8));
    }
    void setEthSeamMask(u16 mask) {
      mEthSeamLo = (u8) (mask & 0xff);
      mEthSeamHi = (u8) (mask >> 8);
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

    /* Keep the LATEST marks: when the log would overflow, drop the oldest
       words to make room (RingBuffer::add alone discards the NEW input, so
       a log nobody drains froze on the earliest marks).  Goes by words, not
       marks: a torn mark at the head is fine, the host dump has plenty of
       unformatted text anyway.  RACY by design against the host's slowscan
       read/reset (both advance mFirstUsedIdx); the producer side is
       serialized by stringPackLock. */
    void makeRoomForWords(u32 words) {
      typedef LogBuffer LB;
      while (LB::RING_BUFFER_MASK -
             ((mLogBuffer.mFirstFreeIdx - mLogBuffer.mFirstUsedIdx) &
              LB::RING_BUFFER_MASK) < words) {
        if (!mLogBuffer.drop()) break;
      }
    }

    bool packString(const char *st) {
      u32 len = 0;
      while (st[len]) ++len;
      makeRoomForWords((len + 1u) >> 1);
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
