/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"

#ifndef BUILD_HOST      
#include "HostBlock.h"
#include "nanoprintf.h"
#endif

namespace MFM {

  struct TCCommon {

    enum TCState : u8 {
      UNUSED = 0u,           // 0 under construction
      OPEN,                  // 1 available for (un)loading locally
      CLOSED,                // 2 finished (un)loading locally
      INBOUND_DEPARTED,      // 3 left t6/ewp or arrived host/hub
      OUTBOUND_DEPARTED,     // 4 left host/hub or arrived t6/ewp
    };

    struct TCMarker {
      static constexpr u8 TCM_MAGIC = 0x2C;

      static constexpr u8 encodePayloadBytesToTCMSize(u32 payb) {
        if (payb > 52u) return (payb+12u)/64u + 3u;
        if (payb > 20u) return 3u; // packet size 64 h+21..52+a+f
        if (payb > 8u) return 2u;  // packet size 32 h+9..20+a+f
        if (payb > 0u) return 1u;  // packet size 16 h+1..8+f
        return 0u;                 // packet size 8 h+f
      }

      static constexpr u32 decodeTCMSizeToPayloadCapacityBytes(u8 tcms) {
        switch (tcms) {
        case 0u: return 0u;
        case 1u: return 8u;
        case 2u: return 20u;
        case 3u: return 52u;
        }
        return (tcms-2u)*64u-12u; // NB: not tcms-3u
      }

      static constexpr u32 getPacketHeaderSizeForPayloadBytes(u32 payb) {
        if (payb <= 8) return 8u; // h+f
        return 12u;               // h+a+f
      }

      static constexpr u32 decodeTCMSizeToPacketSizeBytes(u8 tcms) {
        u32 paycap = decodeTCMSizeToPayloadCapacityBytes(tcms);
        return paycap + getPacketHeaderSizeForPayloadBytes(paycap);
      }

      static constexpr u32 getPacketWordsFromTCMSize(u8 tcms) {
        u32 pktbytes = decodeTCMSizeToPacketSizeBytes(tcms);
        MFM_API_ASSERT((pktbytes%4) == 0,BAD_ALIGNMENT);
        return pktbytes/4u;
      }

      static constexpr u32 getPacketBytesFromTCMSize(u8 tcms) {
        return decodeTCMSizeToPacketSizeBytes(tcms);
      }

      static constexpr u8 getTCMSizeFromPacketSize(u32 pktb) {
        if (pktb <= 16) return encodePayloadBytesToTCMSize(pktb - 8u); // tcm 0 or 1
        return encodePayloadBytesToTCMSize(pktb - 12u); // tcm 2+
      }

      static constexpr u8 getTCMSizeFromPayloadSize(u32 payb) {
        return encodePayloadBytesToTCMSize(payb);
      }

      static constexpr u32 getFooterWordIndex(u8 tcmsize) {
        return getPacketWordsFromTCMSize(tcmsize) - 1u;
      }

      static constexpr bool hasAnkle(u8 tcmsize) { return tcmsize != 0u; }

      static constexpr u32 getAnkleWordIndex(u8 tcmsize) {
        if (!hasAnkle(tcmsize) == 0u) return 0u; // no ankle, return header word index
        return getPacketWordsFromTCMSize(tcmsize) - 2u;
      }

      static constexpr u8 getPacketBytesFromPayloadSize(u32 payb) {
        return getPacketBytesFromTCMSize(getTCMSizeFromPayloadSize(payb));
      }

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
        mTCMSize = getTCMSizeFromPayloadSize(payloadBytes);
        mTCMState = state;
      }

      bool isValid() const {
        return
          mTCMMagic == TCM_MAGIC
          ;
      }
      constexpr TCMarker(u16 payloadBytes = 0u) 
        : mTCMMagic(TCM_MAGIC)
        , mTCMNonce(0u)
        , mTCMSize(getTCMSizeFromPayloadSize(payloadBytes))
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

    struct TCOpsData {
      u32 mArrivalTime;           // in whatever units host vs cross
      u32 mDepartureTime;         // ditto
    };

    union TCWord {
      TCMarker mMarker;
      u32 mWord;
      u8 mBytes[4];

      TCWord() { mWord = 0u; }
      TCWord(const TCWord & other) { mWord = other.mWord; }
    };
    
  };

}
