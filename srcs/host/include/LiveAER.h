#pragma once  /* -*- C++ -*- */
#include "itype.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MFM {

  /* Rough live event rate.  Every hub logs '.ev:C:R:E.' (cumulative event
     counters, hex) when it stops running events; see PhaseSteppers.cpp.
     OurTLBs::updateLogBlocks hands each parsed tile mark to noteMark(),
     which costs one 4 byte compare for any other mark.  We keep only the
     LATEST cumulative events (C+E) of each hub with that mark's own hub
     clock (ms), so a lost mark just makes the next one cover more time:
     nothing here needs every mark.  The host side (LiveAER.py) turns
     snapshots into mAER.  The first '.as' mark (autoseed) sets a flag.
     The tile marks are the LOGPTAG kind: they can be lossy or late, so
     this is a reference figure, not a measurement; aer.py is the
     measurement. */
  struct LiveAER {
    struct HubEvents {
      u32 mKey;            //< chip<<16 | noc x<<8 | noc y
      u64 mEvents;         //< C+E, cumulative since boot
      u64 mHubMs;          //< that hub's own clock at the mark
    };

    static LiveAER & get() ;

    void noteMark(u8 chip, u8 x, u8 y,
                  u64 hubMs, const std::string & msg) ;

    /* Latest events of every hub heard from; sets \c seeded. */
    std::vector<HubEvents> snapshot(bool & seeded) ;

    void clear() ;

    /* '.ev:' payload -> C+E, false if it is not a good '.ev' mark. */
    static bool parseEv(const std::string & msg, u64 & events) ;

  private:
    std::mutex mLock;
    std::unordered_map<u32,HubEvents> mHubs;
    bool mSeeded = false;
  };
}
