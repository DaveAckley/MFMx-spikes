#pragma once   /* -*- C++ -*- */
#include "TCCommon.h"

namespace MFM {

  template <class SUBTC>
  struct TCBase {

    // self(): access this by subtype
    SUBTC& self() { return static_cast<SUBTC&>(*this); }
    SUBTC const & self() const { return static_cast<SUBTC const&>(*this); }

    // "API": SUBTC must implement all of these!
    const char * getName() const { return self().getName(); }
    u32 getMaxPayloadSize() const { return self().getMaxPayloadSize(); }
    u32 getMaxPacketSize() const { return self().getMaxPacketSize(); }
    TCWord getWordAt(u32 word) const { return self().getWordAt(word); }
    TCWord & getWordAt(u32 word) { return self().getWordAt(word); }
    bool readyToClose(TCOpsData & tms, u32 msnow) const { return self().readyToClose(tms,msnow); }
    void reset() { return self().reset(); }

    // SERVICES
    TCState getTCState() const { return getHeader().getTCState(); }

    void writeMarkers(TCMarker m) {
      getHeader() = m;                    // write header
      if (TCMarker::hasAnkle(m.mTCMSize)) // if this size has an ankle
        getAnkleOrDie() = m;              // write it too
      getFooter() = m;                    // finally, write footer
    }

    void resetTC() {
      FAIL(INCOMPLETE_CODE);
    }

    void openTC() {
      TCMarker & h = getHeader();
      h.mTCMState = TCState::OPEN; // no bump nonce, no write markers
    }

    void setDepartingTC(TCState departingState) {
      TCMarker h = getHeader();
      HBASSERT_EQ(h.getTCState(),TCState::CLOSED);
      // tcmSize was set at closing
      h.mTCMNonce++;
      h.mTCMState = departingState;
      writeMarkers(h);
    }

    void closeTC(u32 finalPayloadBytes) {
      MFM_API_ASSERT(finalPayloadBytes <= getMaxPayloadSize(), OUT_OF_ROOM);

      TCMarker h = getHeader();
      MFM_API_ASSERT(h.getTCState() == TCState::OPEN, ILLEGAL_ARGUMENT);

      h.reinit(finalPayloadBytes, TCState::CLOSED); // sets tcmSize here
      if (false) {
        TCWord w;
        w.mMarker = h;
        HBXTAG(hre,w.mWord);
      }
      writeMarkers(h);
      if (true) {
        TCWord w;
        w.mMarker = h;
        HBXTAG(h2e,w.mWord);
        w.mMarker = getHeader();
        HBXTAG(fhh,w.mWord);
        HBXTAG(*hd,&getHeader());
        w.mMarker = getFooter();
        HBXTAG(fft,w.mWord);
        HBXTAG(*ft,&getFooter());
        w.mMarker = getAnkle();
        HBXTAG(fak,w.mWord);
        HBXTAG(*ak,&getAnkleOrDie());
        HBXTAG(TCBase::this,this);
      }
    }

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

    bool isComplete() const {
      TCMarker h = getHeader();
      if (!h.isValid()) return false;
      TCMarker f = getFooter();
      if (!f.isValid()) return false;
      if (h != f) return false;
      if (TCMarker::hasAnkle(h.mTCMSize)) {
        TCMarker a = getAnkle();
        if (!a.isValid()) return false;
        if (h != a) return false;
      }
      return true;
    }
    
  protected:
    TCBase() = default; // don't make these
    ~TCBase() = default; 

  };
}
