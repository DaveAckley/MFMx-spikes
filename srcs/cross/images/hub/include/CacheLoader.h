#pragma once    /* -*- C++ -*- */
#include "NgbCacheMgr.h"  //< for sites-and-caches constants
#include "Grid.h"         //< for GridManager
#include "PT_InterHub.h"  //< for InterHubBlock

namespace MFM {

  struct CacheLoader {

    U8CRange::iterator mLocalItr; //< in local coords
    U8CRange::iterator mRemoteItr; //< in their local coords
    Dir8 mLeaderDir8; //< where leader is from us (for routing?)
    u8 mCacheLoaderIndex;

    void init() {
      memset_s(this, '\0', sizeof(*this));
    }

    bool loadPacketFromCache(GridManager & gm, InterHubBlock & ihb) ;

    bool tryLoad(GridManager & gm) ;
    
  };

  struct CacheLoaders {
    static constexpr u32 CACHELOADER_COUNT = 4;
    CacheLoader mCacheLoaders[CACHELOADER_COUNT];
    u8 mCacheLoadersInUse;
    void init() {
      memset_s(this, '\0', sizeof(*this));
    }
    CacheLoader * getIfAny(Dir8 d8) {
      for (u32 i = 0; i < mCacheLoadersInUse; ++i) {
        CacheLoader & cl = get(i);
        if (cl.mLeaderDir8 == d8) return &cl;
      }
      return nullptr;
    }

    CacheLoader & allocate() {
      CacheLoader & ret = get(mCacheLoadersInUse);
      ret.mCacheLoaderIndex = mCacheLoadersInUse++;
      return ret;
    }
    CacheLoader & get(u32 idx) {
      MFM_API_ASSERT(idx < CacheLoaders::CACHELOADER_COUNT, ILLEGAL_ARGUMENT);
      return mCacheLoaders[idx];
    }
  };

  extern CacheLoaders theL1CacheLoaders;
}
