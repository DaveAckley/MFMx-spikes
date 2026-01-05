#pragma once /* -*- C++ -*- */

#include "itype.h"
#include "S32C.h"
#include "OurMutex.h"
#include "TimeDefs.h"

/** Dave's Cheezo Fixed-Radius Near Neighbors implementation.
    Cheezoifications include:

    - Accept BUCKET_RADIUS separately from LOCK_RADIUS to trade off
      bucket size vs buckets searched?

    - Use index array instead of linked list for 'better locality'
      when searching a bucket?

 */

using namespace MFM;
template<s32 GRID_X_MIN, s32 GRID_Y_MIN, s32 GRID_X_MAX, s32 GRID_Y_MAX, u32 LOCK_RADIUS, u32 LOCK_USEC>
struct STVL {

  struct Entry {
    S32C mPosition;
    TimeStamp mWhenAllocated;
    bool operator==(const Entry & other) const {
      return
        mPosition == other.mPosition &&
        mWhenAllocated == other.mWhenAllocated;
    }
  };
  
  STVL() ;
  bool tryLock(S32C center, Entry & token) ;
  bool unlock(Entry token) ;

  static constexpr u32 GRID_WIDTH = (u32) (GRID_X_MAX-GRID_X_MIN + 1);
  static constexpr u32 GRID_HEIGHT = (u32) (GRID_Y_MAX-GRID_Y_MIN + 1);
  static constexpr u32 BUCKET_RADIUS = 2*LOCK_RADIUS;
  static constexpr u32 XBUCKETS = (GRID_WIDTH+BUCKET_RADIUS-1)/BUCKET_RADIUS;
  static constexpr u32 YBUCKETS = (GRID_HEIGHT+BUCKET_RADIUS-1)/BUCKET_RADIUS;
  static constexpr u32 MAX_PER_BUCKET = BUCKET_RADIUS*BUCKET_RADIUS/LOCK_RADIUS;

  typedef std::pair<u32,u32> BucketIndex;

  u64 getFaile() const { return mFailed; }
  u64 getRejected() const { return mRejected; }
  u64 getAllocated() const { return mAllocated; }
  u64 getFreed() const { return mFreed; }
  u64 getExpired() const { return mExpired; }

private:
  bool getCenterBucketIndex(S32C pt, BucketIndex & bi) const {
    if (pt.x < GRID_X_MIN || pt.x > GRID_X_MAX ||
        pt.y < GRID_Y_MIN || pt.y > GRID_Y_MAX)
      return false;
    u32 x = (pt.x - GRID_X_MIN)/BUCKET_RADIUS;
    u32 y = (pt.y - GRID_Y_MIN)/BUCKET_RADIUS;
    bi.first = x;
    bi.second = y;
    return true;
  }

  OurMutex mSTVLMutex;
  u64 mFailed;                  //< #ran out of room in bucket
  u64 mRejected;                //< #centers already locked
  u64 mAllocated;               //< #centers locked successfully
  u64 mFreed;                   //< #timely lock releases
  u64 mExpired;                 //< #late locks reaped

  struct Bucket {
    Entry mEntries[MAX_PER_BUCKET];
    u32 mInUse;
  };

  Bucket mBuckets[XBUCKETS][YBUCKETS];

};

#include "STVL.tcc"
