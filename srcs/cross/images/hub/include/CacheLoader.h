#pragma once    /* -*- C++ -*- */
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "Grid.h"         //< for GridManager
#include "PT_InterHub.h"  //< for InterHubBlock

namespace MFM {

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< in local coords
    U8CRange::iterator mRemoteItr; //< in their local coords
    Dir8 mDir8; //< our index in cacheloaders (and where leader/follower is from us?)
    s8 mDestType; //< >0 -> leader, <0 -> follower, =0 -> uninitted/not-in-use (e.g., off-edge)

    void init(Dir8 d8) {
      memset_s(this, '\0', sizeof(*this));
      mDir8 = d8;
    }

    bool loadPacket(bool localNotRemote, GridManager & gm, InterHubBlock & ihb) ;

    bool unloadPacket(bool localNotRemote, GridManager & gm, InterHubBlock & ihb) ;

    bool tryLoad(GridManager & gm) ;
    
  };

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 8;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mCurrentSCL;
    u8 mNextSCL;

    void configureCacheLoaders(u8 nextSCLeader) ;

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
  };

  extern CacheLoaders theL1CacheLoaders;
}
