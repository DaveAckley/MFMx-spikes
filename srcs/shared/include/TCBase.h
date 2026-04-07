#pragma once   /* -*- C++ -*- */
#include "TCCommon.h"

namespace MFM {

  template <class SUBTC>
  struct TCBase {

#ifndef BUILD_HOST      
    void checkSizeBlow(TCMarker h,u32 payb,const char * file, unsigned line) const { 
      u8 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
      if (!(fi > 0u && fi < getMaxWordSize())) {
        extern HostBlock theHostBlock;
        char buf[100];
        npf_snprintf(buf,100,"CSBL [%u] p%luB %s fi%u max%lu h:0x%02x %u %u(%luW/%luB) %u\n",
                     line,payb,
                     getName(), fi, getMaxWordSize(),
                     h.mTCMMagic,
                     h.mTCMNonce,
                     h.mTCMSize, h.getPacketWords(), h.getPacketBytes(),
                     h.mTCMState);
        theHostBlock.packString(buf);
        FATAL_AT(FAILCode::OUT_OF_ROOM,file,line);
      }
    }
#else
    void checkSizeBlow(TCMarker h,u32 payb,const char * file, unsigned line) const { }
#endif

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
#ifndef BUILD_HOST      
      if (false) {
        extern HostBlock theHostBlock;
        char buf[100];
        u8 tcms = m.mTCMSize;
        u32 aidx = TCMarker::getAnkleWordIndex(tcms);
        u32 fidx = TCMarker::getFooterWordIndex(tcms);
        u32 av = getWordAt(aidx).mWord;
        u32 fv = getWordAt(fidx).mWord;
        npf_snprintf(buf,100,"wM- s%u pay %luB a[%lu]=0x%08lx f[%lu]=0x%08lx\n",
                     tcms, m.getPayloadCapacity(), aidx, av, fidx, fv);
        theHostBlock.packString(buf);
      }
#endif

      getHeader() = m;                    // write header
      if (TCMarker::hasAnkle(m.mTCMSize)) // if this size has an ankle
        getAnkleOrDie() = m;              // write it too
      getFooter() = m;                    // finally, write footer

#ifndef BUILD_HOST      
      if (false) {
        extern HostBlock theHostBlock;
        char buf[100];
        m = getHeader();
        u8 tcms = m.mTCMSize;
        u32 aidx = TCMarker::getAnkleWordIndex(tcms);
        u32 fidx = TCMarker::getFooterWordIndex(tcms);
        u32 av = getWordAt(aidx).mWord;
        u32 fv = getWordAt(fidx).mWord;
        npf_snprintf(buf,100,"wM+ s%u pay %luB a[%lu]=0x%08lx f[%lu]=0x%08lx\n",
                     tcms, m.getPayloadCapacity(), aidx, av, fidx, fv);
        theHostBlock.packString(buf);
      }
#endif
    
    }

    //    u32 getPacketWords() const { return getHeader().getPacketWords(); }
    //    u32 getPacketBytes() const { return getHeader().getPacketBytes(); }

    void resetTC() {
      FAIL(INCOMPLETE_CODE);
    }

    void openTC() {
      TCMarker & h = getHeader();
      h.mTCMState = TCState::OPEN; // no bump nonce, no write markers
    }

    void setDepartingTC(TCState departingState) {
      TCMarker h = getHeader();
      MFM_API_ASSERT(h.getTCState() == TCState::CLOSED, ILLEGAL_STATE);
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
      writeMarkers(h);
    }

    u32 getMaxWordSize() const { return getMaxPacketSize()/4u; }

    TCMarker getMarkerAt(u32 word) const { return getWordAt(word).mMarker; }
    TCMarker & getMarkerAt(u32 word) { return getWordAt(word).mMarker; }

    TCMarker getHeader() const { return getMarkerAt(0); }
    TCMarker & getHeader() { return getMarkerAt(0); }

    TCMarker getAnkle() const {
      TCMarker h = getHeader();
      checkSizeBlow(h,0u,__FILE__,__LINE__);

      u8 ai = TCMarker::getAnkleWordIndex(h.mTCMSize); // will be 0u if no ankle
      if (ai == 0u) return h;                          // and then return header
      MFM_API_ASSERT(ai < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(ai).mMarker;
    }

    TCMarker & getAnkleOrDie() {
      TCMarker h = getHeader();
      checkSizeBlow(h,0u,__FILE__,__LINE__);

      u8 ai = TCMarker::getAnkleWordIndex(h.mTCMSize); 
      MFM_API_ASSERT(ai > 0u && ai < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS); // die if no ankle
      return getWordAt(ai).mMarker;
    }

    TCMarker getFooter() const {
      TCMarker h = getHeader();
      checkSizeBlow(h,0u,__FILE__,__LINE__);

      u8 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
      MFM_API_ASSERT(fi > 0u && fi < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(fi).mMarker;
    }

    TCMarker & getFooter() {
      TCMarker h = getHeader();

      checkSizeBlow(h,0u,__FILE__,__LINE__);
      u8 fi = TCMarker::getFooterWordIndex(h.mTCMSize);
        
      MFM_API_ASSERT(fi > 0u && fi < getMaxWordSize(),ARRAY_INDEX_OUT_OF_BOUNDS);
      return getWordAt(fi).mMarker;
    }

    /* TEST DEIMPLEMENTATION
    bool isEmpty() { return getHeader().mTCMSize == TCMarker::encodePayloadBytesToTCMSize(0u); }
    */

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
    
  protected:
    TCBase() = default; // don't make these
    ~TCBase() = default; 

  };
}
