#pragma once    /* -*- C++ -*- */
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "Grid.h"         //< for GridManager
#include "PT_InterHub.h"  //< for InterHubBlock

namespace MFM {

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< a site or cache range, in local coords
    Dir8 mDir8; //< our index in cacheloaders (and where leader/follower is from us?)
    s8 mDestType; //< >0 -> leader, <0 -> follower, =0 -> uninitted or not-in-use (e.g., off-edge)

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

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 8;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mConfiguredSCL;

    void configureCacheLoaders(u8 scleader) ;

    u8 getConfiguredSCL() const { return mConfiguredSCL; }

    void saveCaches() ;

    void loadCaches() ;

    void restartIterators() {
      for (u32 i = 0; i < CACHELOADER_COUNT; ++i) 
        get(i).restartIterator();
    }

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
