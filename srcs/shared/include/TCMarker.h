#pragma once   /* -*- C++ -*- */
#include "TCState.h"
#include "Debug.h"

namespace MFM {
  struct TCMarker {
    static constexpr u8 TCM_MAGIC = 0x2C;

    static constexpr u32 ceilDiv(u32 num, u32 den) {
      return num/den + ((num%den) != 0);
    }
    static constexpr u8 encodePayloadBytesToTCMSize(u32 payb) {
      if (payb <= 8u) return 0u;    // packet size 16 h+1..8+f
      if (payb <= 24u) return 1u;   // packet size 32 h+9..24+f
      if (payb <= 40u) return 2u;   // packet size 64 h+25..40+a+f+3p
        
      u8 tcms = ceilDiv((payb-40),64) + 2u;
      MFM_API_ASSERT(tcms < 255u, ILLEGAL_ARGUMENT);
      return tcms;
    }

    static constexpr u32 decodeTCMSizeToPayloadCapacityBytes(u8 tcms) {
      switch (tcms) {
      case 0u: return 8u;
      case 1u: return 24u;
      case 2u: return 40u;
      }
      return (tcms-2u)*64u+40u;
    }

    static constexpr u32 getPacketOverheadBytesForTCMSize(u8 tcms) {
      if (tcms <= 1u) return 8u; // h=4B + f=4B
      return 24u;                // h=4B + a=4B + last segment padding=12B + f=4B
    }

    static constexpr u32 getPacketOverheadBytesForPayloadSize(u32 payb) {
      u8 tcms = encodePayloadBytesToTCMSize(payb);
      return getPacketOverheadBytesForTCMSize(tcms);
    }

    static constexpr u32 decodeTCMSizeToPacketSizeBytes(u8 tcms) {
      u32 paycap = decodeTCMSizeToPayloadCapacityBytes(tcms);
      u32 overhead =  getPacketOverheadBytesForTCMSize(tcms);
      return paycap + overhead;
    }

    static constexpr u32 getPacketWordsFromTCMSize(u8 tcms) {
      MFM_API_ASSERT(tcms < 255u, ILLEGAL_ARGUMENT);
      u32 pktbytes = decodeTCMSizeToPacketSizeBytes(tcms);
      MFM_API_ASSERT((pktbytes%4) == 0,BAD_ALIGNMENT);
      return pktbytes/4u;
    }

    static constexpr u32 getPacketBytesFromTCMSize(u8 tcms) {
      return decodeTCMSizeToPacketSizeBytes(tcms);
    }

#if 0      
    static constexpr u8 getTCMSizeFromPayloadSize(u32 payb) {
      return encodePayloadBytesToTCMSize(payb);
    }
#endif

    static constexpr u32 getFooterWordIndex(u8 tcmsize) {
      u32 words = getPacketWordsFromTCMSize(tcmsize);
      MFM_API_ASSERT(words>0, ILLEGAL_STATE); 
      return words - 1u; // always last word of packet
    }

    static constexpr bool hasAnkle(u8 tcmsize) { return tcmsize >= 2u; }

    static constexpr u32 getAnkleWordIndex(u8 tcmsize) {
      if (!hasAnkle(tcmsize)) return 0u; // no ankle, return header word index
      return getPacketWordsFromTCMSize(tcmsize) - 5u; // always 5w back to get on other stream
    }

    static constexpr u32 getPacketBytesFromPayloadSize(u32 payb) {
      u8 tcms = encodePayloadBytesToTCMSize(payb);
      return getPacketBytesFromTCMSize(tcms);
    }

    u32 getPayloadCapacity() const { return decodeTCMSizeToPayloadCapacityBytes(mTCMSize); }

    u32 getPacketBytes() const { return getPacketBytesFromTCMSize(mTCMSize); }

    u32 getPacketWords() const { return getPacketWordsFromTCMSize(mTCMSize); }

    void init(u8 initnonce = 0u) {
      mTCMMagic = TCM_MAGIC;
      mTCMNonce = initnonce;
      mTCMSize = 0u;          // code 0 -> 2words / 8bytes
      mTCMState = TCState::UNUSED;
    }

    void reinit(u32 payloadBytes, TCState state) {
      mTCMMagic = TCM_MAGIC;
      mTCMNonce++;
      mTCMSize = encodePayloadBytesToTCMSize(payloadBytes);
      /*
      HBPTAG(payb,payloadBytes);
      HBPTAG(tcms,mTCMSize);
      HBPTAG(decp,decodeTCMSizeToPayloadCapacityBytes(mTCMSize));
      mTCMState = state;
      HBPTAG(nc,(u32)mTCMNonce);
      HBPTAG(fi,getFooterWordIndex(mTCMSize));
      HBPTAG(ai,getAnkleWordIndex(mTCMSize));
      */
    }

    bool isValid() const {
      return
        mTCMMagic == TCM_MAGIC
        ;
    }
    constexpr TCMarker(u16 payloadBytes = 0u) 
      : mTCMMagic(TCM_MAGIC)
      , mTCMNonce(0u)
      , mTCMSize(encodePayloadBytesToTCMSize(payloadBytes))
      , mTCMState(TCState::UNUSED)
    { }
    constexpr TCMarker(const TCMarker& other) 
      : mTCMMagic(other.mTCMMagic)
      , mTCMNonce(other.mTCMNonce)
      , mTCMSize(other.mTCMSize)
      , mTCMState(other.mTCMState)
    { }

    TCMarker & operator=(const TCMarker & other) {
      mTCMMagic = other.mTCMMagic;
      mTCMNonce = other.mTCMNonce;
      mTCMSize = other.mTCMSize;
      mTCMState = other.mTCMState;
      return *this;
    }
      bool operator==(const TCMarker & other) const {
        return isValid()
        && mTCMMagic == other.mTCMMagic
        && mTCMNonce == other.mTCMNonce
        && mTCMSize == other.mTCMSize
        && mTCMState == other.mTCMState
        ;
      }
    bool operator!=(const TCMarker & other) const {
      return !(*this == other);
    }

    TCState getTCState() const { return (TCState) mTCMState; }

    u8 mTCMMagic;
    u8 mTCMNonce;
    u8 mTCMSize;
    u8 mTCMState;
  };
}
