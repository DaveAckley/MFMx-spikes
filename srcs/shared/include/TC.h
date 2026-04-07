/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"
#include "TCBase.h"
#include "TCMarker.h"

namespace MFM {

  template<class SUBSUBTC, u32 MAX_PAYLOAD_BYTES>
  struct alignas(16) TC : public TCBase<SUBSUBTC> {
    static constexpr u32 MAX_PAYLOAD_SIZE = MAX_PAYLOAD_BYTES;

    using Self = TC<SUBSUBTC,MAX_PAYLOAD_SIZE>; 
    //    using typename TCBase<SUBSUBTC>::TCMarker;
    //    using typename TCBase<SUBSUBTC>::TCWord;

    static constexpr u32 MAX_PACKET_SIZE = TCMarker::getPacketBytesFromPayloadSize(MAX_PAYLOAD_SIZE);
    //static_assert((MAX_PACKET_SIZE >= MAX_PAYLOAD_SIZE),"NOT A REAL CLAIM JUST DEBUG");
    static_assert((MAX_PACKET_SIZE >= 8),"MAX_PACKET_SIZE TOO SMALL");
    static_assert((MAX_PACKET_SIZE%4 == 0),"MAX_PACKET_SIZE%4 != 0");

    static constexpr u32 MAX_PACKET_WORDS = MAX_PACKET_SIZE/4u;

    // TCBase API

    u32 getMaxPayloadSize() const { return MAX_PAYLOAD_SIZE; }
    u32 getMaxPacketSize() const {
      static_assert(sizeof(*this)==MAX_PACKET_SIZE,"BAX MAD");
      return sizeof(*this);
    }
    TCWord getWordAt(u32 word) const {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }
    TCWord & getWordAt(u32 word) {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }

    void reset() {
      memset_s(this,'\0',sizeof(*this));
      this->getHeader() = TCMarker(getMaxPayloadSize());
    }

    // SUBCLASS OF TC MUST IMPLEMENT:
    //bool readyToClose(TCOpsData & tms, u32 msnow) const { FAIL(INCOMPLETE_CODE); }

    void* getDataStart() { return (void*) &mWords[1]; }

  protected:
    // TC() = default; // don't make these

    TCWord mWords[MAX_PACKET_WORDS]; // [0] is always marker; rest could also be data or unused
  };

}
