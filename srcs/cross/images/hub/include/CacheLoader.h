#pragma once    /* -*- C++ -*- */
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "Grid.h"         //< for GridManager
#include "PT_InterHub.h"  //< for InterHubBlock

namespace MFM {

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< a site or cache range, in local coords
    Dir8 mDir8; //< our index in cacheloaders (and where leader/follower is from us?)
    s8 mDestType; //< >0 -> leader, <0 -> follower, =0 -> uninitted or not-in-use (e.g., off-edge)

    bool isThatAFollower() const { return mDestType < 0; }
    bool isThatALeader() const { return mDestType > 0; }
    bool isThatUnused() const { return mDestType == 0; }

    void init(Dir8 d8) {
      memset_s(this, '\0', sizeof(*this));
      mDir8 = d8;
    }

    bool needsAnotherPacket() const { return mDestType != 0 && !mLocalItr.atEnd(); }

    void loadPacketIfNeeded(GridManager & gm) ;

    void unloadPacketIfNeeded(GridManager & gm) ;

    void loadPacket(GridManager & gm, InterHubBlock & ihb) ;

    void unloadPacket(GridManager & gm, InterHubBlock & ihb) ;

    void restartIterator() { mLocalItr.restart(); }
    
  };

  /* Busy-wait after every packet moved (was an unnamed DEBUG literal of
     10'000'000, 'give hn a chance').  Named so the exchange timing can be
     measured and tuned (the .xb/.xe marks).  EXPERIMENT history: 10'000'000
     (64 ms per packet, duty 74%), 1'000'000 (6.4 ms, duty 94%, run 123006),
     now 100'000; the last candidate is 0. */
  static constexpr u32 PACKET_SLEEP_CYCLES = 100'000u;

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 8;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mConfiguredSCL;
    u32 mPacketsMoved;          //< packets loaded or unloaded since cleared (.xe mark)

    void configureCacheLoaders(u8 scleader) ;

    u8 getConfiguredSCL() const { return mConfiguredSCL; }

    void saveCaches() ;

    void loadCaches() ;

    void restartIterators() {
      for (u32 i = 0; i < CACHELOADER_COUNT; ++i) 
        get(i).restartIterator();
    }

    /* One line of who-is-waiting state for the .xw heartbeat: hex of the
       need mask (bit per Dir8 that still wants a packet), the leader mask,
       then per Dir8 the lengths of the inbound and outbound car rings. */
    void formatWaiting(char * buf, u32 len) ;

    bool allReadyToAck() const {
      for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
        if (get(i).needsAnotherPacket()) return false;
      }
      return true;
    }
    
    void init() {
      memset_s(this, '\0', sizeof(*this));
      for (u32 i = 0; i < CACHELOADER_COUNT; ++i) {
        get(i).init((Dir8) i);
      }
    }

    CacheLoader & get(u32 idx) {
      HBASSERT_LT(idx,CacheLoaders::CACHELOADER_COUNT); // ILLEGAL_ARGUMENT
      return mCacheLoaders[idx];
    }

    const CacheLoader & get(u32 idx) const {
      HBASSERT_LT(idx,CacheLoaders::CACHELOADER_COUNT); // ILLEGAL_ARGUMENT
      return mCacheLoaders[idx];
    }
  };

  extern CacheLoaders theL1CacheLoaders;
}
