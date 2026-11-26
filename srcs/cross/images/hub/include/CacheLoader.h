#pragma once    /* -*- C++ -*- */
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "Grid.h"         //< for GridManager
#include "PT_InterHub.h"  //< for InterHubBlock

namespace MFM {

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< a site or cache range, in local coords
    /* Policy ewsnap/ewsnap10: the band as a walk over whole tiles
       (EWLattice.h, radius 4 or 10); mLocalItr is unused then. */
    EWAnyBandWalk mBand;
    u8 mEWSnap;                   //< walk mBand instead of mLocalItr
    Dir8 mDir8; //< our index in cacheloaders (and where leader/follower is from us?)
    s8 mDestType; //< >0 -> leader, <0 -> follower, =0 -> uninitted or not-in-use (e.g., off-edge)

    bool isThatAFollower() const { return mDestType < 0; }
    bool isThatALeader() const { return mDestType > 0; }
    bool isThatUnused() const { return mDestType == 0; }

    void init(Dir8 d8) {
      memset_s(this, '\0', sizeof(*this));
      mDir8 = d8;
    }

    bool walkDone() const {
      return mEWSnap ? mBand.atEnd() : mLocalItr.atEnd();
    }

    bool needsAnotherPacket() const { return mDestType != 0 && !walkDone(); }

    void loadPacketIfNeeded(GridManager & gm) ;

    void unloadPacketIfNeeded(GridManager & gm) ;

    void loadPacket(GridManager & gm, InterHubBlock & ihb) ;

    void unloadPacket(GridManager & gm, InterHubBlock & ihb) ;

    void restartIterator() {
      if (mEWSnap) mBand.restart();
      else mLocalItr.restart();
    }
    
  };

  /* Busy-wait after every packet moved (was an unnamed DEBUG literal of
     10'000'000, 'give hn a chance').  Named so the exchange timing can be
     measured and tuned (the .xb/.xe marks).  EXPERIMENT history: 10'000'000
     (64 ms per packet, duty 74%), 1'000'000 (6.4 ms, duty 94%, run 123006),
     100'000 (0.8 ms, duty 97%; one hang in two one-hour runs, 125641 hung,
     165950 clean), now 0 (run seam10.cmd). */
  static constexpr u32 PACKET_SLEEP_CYCLES = 0u;

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 8;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mConfiguredSCL;
    u32 mPacketsMoved;          //< packets loaded or unloaded since cleared (.xe mark)
    u8 mMovedPerDir[CACHELOADER_COUNT]; //< same, per Dir8, saturating at 255 (.xe mark)
    u8 mKeepMask;               //< bit per loader: kept across this SAVE and the next LOAD (elision)
    u8 mHaveLoaded;             //< a LOAD has completed: the borrowers hold real caches

    /* CACHE ELISION (four corners, host flag HBF_ELIDE_CACHES).  A direction
       whose role is the same under the previous corner and the next one
       (both ends see the same role, the corner is the same everywhere) needs
       neither the SAVE of this NEW_LEADER nor the LOAD of the next
       RUN_EVENTS: the borrower keeps its cache, the lender keeps its sites
       untouched (it stays clear of the band it lends).  Computed once at the
       start of each SAVE from the new corner.  Nothing is kept before a LOAD
       has happened. */
    void setKeepMaskFor(s32 nextCorner) ;
    void countMoved(u32 idx) {
      if (mMovedPerDir[idx] != 255u) ++mMovedPerDir[idx];
    }

    bool isKept(u32 idx) const { return (mKeepMask & (1u << idx)) != 0u; }

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
        if (isKept(i)) continue;
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
