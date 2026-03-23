/* -*- C++ -*- */
#pragma once

#include "itype.h"
#include "Fail.h"
#include "TCCommon.h"

namespace MFM {

  template <u32 MAX_PAYLOAD_SIZE>
  struct TC : public TCBase {
    static constexpr u32 MAX_PACKET_SIZE = TCMarker::getPacketBytesFromPayloadSize(MAX_PAYLOAD_SIZE);
    static_assert(((MAX_PACKET_SIZE >= 8) &&
                   ((MAX_PACKET_SIZE%4) == 0)),
                  "Bad TC packet size");
    static constexpr u32 MAX_PACKET_WORDS = MAX_PACKET_SIZE/4u;

    // TCBase API
    //bool readyToClose(TCOpsData & tms, u32 msnow) const { return self().readyToCloseTC(tms,msnow); }
    //void reset() { self().resetTC(); }

    u32 getMaxPacketSize() const override { return sizeof(*this); }
    TCWord getWordAt(u32 word) const override {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }
    TCWord & getWordAt(u32 word) override {
      if (word < MAX_PACKET_WORDS) return mWords[word];
      FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
    }

    void* getDataStart() { return (void*) &mWords[1]; }

  protected:
    // TC() = default; // don't make these

    TCWord mWords[MAX_PACKET_WORDS]; // [0] is always marker; rest could also be data or unused
  };

}
