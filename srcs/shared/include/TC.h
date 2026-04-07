/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"
#include "TCCommon.h"

namespace MFM {

  template< u32 MAX_PAYLOAD_BYTES>
  struct TC : public TCBase<TC<MAX_PAYLOAD_BYTES>> {
    static constexpr u32 MAX_PAYLOAD_SIZE = MAX_PAYLOAD_BYTES;

    using Super = TC<MAX_PAYLOAD_SIZE>; 
    using typename TCBase<Super>::TCMarker;
    using typename TCBase<Super>::TCWord;

    static constexpr u32 MAX_PACKET_SIZE = TCMarker::getPacketBytesFromPayloadSize(MAX_PAYLOAD_SIZE);
    static_assert(((MAX_PACKET_SIZE >= 8) &&
                   ((MAX_PACKET_SIZE%4) == 0)),
                  "Bad TC packet size");
    static constexpr u32 MAX_PACKET_WORDS = MAX_PACKET_SIZE/4u;

    // TCBase API

    u32 getMaxPayloadSize() const { return MAX_PAYLOAD_SIZE; }
    u32 getMaxPacketSize() const { return sizeof(*this); }
    TCWord getWordAt(u32 word) const {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }
    TCWord & getWordAt(u32 word) {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }

    void reset() { memset_s(this,'\0',sizeof(*this)); }

    // SUBCLASS OF TC MUST IMPLEMENT:
    //bool readyToClose(TCOpsData & tms, u32 msnow) const { FAIL(INCOMPLETE_CODE); }

    void* getDataStart() { return (void*) &mWords[1]; }

  protected:
    // TC() = default; // don't make these

    TCWord mWords[MAX_PACKET_WORDS]; // [0] is always marker; rest could also be data or unused
  };

}
