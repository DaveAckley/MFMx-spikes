/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"

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

      static constexpr u32 getPacketWordsFromTCMSize(u8 tcmsize) {
        switch (tcmsize) {
        case 0u: return 2u;   // 0:h, 1:f
        case 1u: return 5u;   // 0:h, 1:p1 2:p2 3:a 4:f
        case 2u: return 9u;   // 0:h, 1-6:ps 7:a 8:f
        }
        return 16u*(tcmsize-2u)+2u; // 0:h, pw-2:a pw-1:f
      }

      static constexpr u32 getPacketBytesFromTCMSize(u8 tcmsize) {
        return 4u*getPacketWordsFromTCMSize(tcmsize);
      }

      static constexpr u32 getFooterWordIndex(u8 tcmsize) {
        return getPacketWordsFromTCMSize(tcmsize) - 1u;
      }

      static constexpr bool hasAnkle(u8 tcmsize) { return tcmsize != 0u; }

      static constexpr u32 getAnkleWordIndex(u8 tcmsize) {
        if (!hasAnkle(tcmsize) == 0u) return 0u; // no ankle, return header word index
        return getPacketWordsFromTCMSize(tcmsize) - 2u;
      }

      static u8 getTCMSizeFromPacketSize(u32 pktb) {
        if (pktb >= 68) return (pktb-4u)/64u + 2u;
        if (pktb >= 36u) return 2u;
        if (pktb >= 20u) return 1u;
        return 0u;
      }

      static constexpr u8 getTCMSizeFromPayloadSize(u32 payb) {
        if (payb == 0u) return 0u;
        if (payb <= 8u) return 1u;
        if (payb <= 24u) return 2u;
        u32 size = getTCMSizeFromPacketSize(payb + 12u);
        if (size <= 255u) return (u8) size;
        FAIL(ILLEGAL_ARGUMENT);
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

  struct TCBase : public TCCommon {
    // API

    virtual u32 getMaxPacketSize() const = 0;

    virtual TCWord getWordAt(u32 word) const = 0;
    virtual TCWord & getWordAt(u32 word) = 0;

    virtual bool readyToClose(TCOpsData & tms, u32 msnow) const = 0;
    virtual void reset() = 0;

    // SERVICES
    u32 getMaxWordSize() const { return getMaxPacketSize()/4u; }

    TCMarker getMarkerAt(u32 word) const { return getWordAt(word).mMarker; }
    TCMarker & getMarkerAt(u32 word) { return getWordAt(word).mMarker; }

    TCMarker getHeader() const { return getMarkerAt(0); }
    TCMarker & getHeader() { return getMarkerAt(0); }

    TCMarker getAnkle() const {
      TCMarker h = getHeader();
      u8 ai = TCMarker::getAnkleWordIndex(h.mTCMSize); // will be 0u if no ankle
      if (ai == 0u) return h;                          // and then return header
      MFM_API_ASSERT(ai < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(ai).mMarker;
    }

    TCMarker & getAnkleOrDie() {
      TCMarker h = getHeader();
      u8 ai = TCMarker::getAnkleWordIndex(h.mTCMSize); 
      MFM_API_ASSERT(ai > 0u && ai < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS); // die if no ankle
      return getWordAt(ai).mMarker;
    }

    TCMarker getFooter() const {
      TCMarker h = getHeader();
      u8 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
      MFM_API_ASSERT(fi > 0u && fi < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(fi).mMarker;
    }

    TCMarker & getFooter() {
      TCMarker h = getHeader();
      u8 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
      MFM_API_ASSERT(fi > 0u && fi < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(fi).mMarker;
    }

    bool isEmpty() { return getHeader().mTCMSize == TCMarker::getTCMSizeFromPayloadSize(0u); }

    bool isComplete() const {
      TCMarker h = getHeader();
      if (!h.isValid()) return false;
      u32 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
      TCMarker f = getMarkerAt(fi);
      if (!f.isValid()) return false;
      if (h != f) return false;
      if (h.mTCMSize > 0u) {
        u32 ai = TCMarker::getAnkleWordIndex(h.mTCMSize);
        TCMarker a = getMarkerAt(ai);
        if (!a.isValid()) return false;
        if (h != a) return false;
      }
      return true;
    }
    
    TCState getTCState() const { return getHeader().getTCState(); }

  protected:
    TCBase() = default; // don't make these
    ~TCBase() = default; 

  };
}
