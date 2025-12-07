/* -*- C++ -*- */
#include "HostUtils.h"

using namespace MFM;

template<s32 XMIN, s32 YMIN, s32 XMAX, s32 YMAX, u32 LOCK_RADIUS, u32 LOCK_USEC>
STVL<XMIN,YMIN,XMAX,YMAX,LOCK_RADIUS,LOCK_USEC>::STVL()
  : mSTVLMutex("STVL")
{
  memset_s((void*) mBuckets,0,sizeof(mBuckets));
}

template<s32 XMIN, s32 YMIN, s32 XMAX, s32 YMAX, u32 LOCK_RADIUS, u32 LOCK_USEC>
bool STVL<XMIN,YMIN,XMAX,YMAX,LOCK_RADIUS,LOCK_USEC>::tryLock(S32C center, Entry & token) {
  BucketIndex bi;
  if (getCenterBucketIndex(center,bi)) {
    OurScopeLock guard(mSTVLMutex);
    TimeStamp now = std::chrono::steady_clock::now();
    TimeStamp expiration = now - TimeDurationMicros(LOCK_USEC);
    for (s32 dy = -1; dy <= +1; ++dy) {
      u32 by = bi.second + dy;
      if (by >= YBUCKETS) continue;  // perhaps by underflow
      for (s32 dx = -1; dx <= +1; ++dx) {
        u32 bx = bi.first + dx;
        if (bx >= XBUCKETS) continue; // ditto
        Bucket & bkt = mBuckets[bx][by];
        // first reap expired
        for (u32 i = 0u; i < bkt.mInUse; ++i) {
        again:
          Entry & e = bkt.mEntries[i];
          if (e.mWhenAllocated < expiration) {
            ++mExpired;
            if (--bkt.mInUse > i) {
              bkt.mEntries[i] = bkt.mEntries[bkt.mInUse];
              goto again; // we have a new entry[i]
            }
          }
        }
        // now check distances
        for (u32 i = 0u; i < bkt.mInUse; ++i) {
          Entry & e = bkt.mEntries[i];
          u32 dist = center.manhattanDistance(e.mPosition);
          if (dist < 2*LOCK_RADIUS)
            return false;       // we're blown
        }
        // we survived this bucket
      }
    }
    // we survived all buckets
    Bucket & cbkt = mBuckets[bi.first][bi.second];
    if (cbkt.mInUse >= MAX_PER_BUCKET) {
      ++mFailed; // doh!
      return false;
    }
    Entry & e = cbkt.mEntries[cbkt.mInUse++];

    // Finally
    e.mPosition = center;       // record lock
    e.mWhenAllocated = now;     // info
    token = e;                  // also return for later
    ++mAllocated;
    return true;
  }
  return false;
}

template<s32 XMIN, s32 YMIN, s32 XMAX, s32 YMAX, u32 LOCK_RADIUS, u32 LOCK_USEC>
bool STVL<XMIN,YMIN,XMAX,YMAX,LOCK_RADIUS,LOCK_USEC>::unlock(Entry token) {
  BucketIndex bi;
  if (getCenterBucketIndex(token.mPosition,bi)) {
    OurScopeLock guard(mSTVLMutex);
    Bucket & bkt = mBuckets[bi.first][bi.second];
    for (u32 i = 0u; i < bkt.mInUse; ++i) {
      Entry & e = bkt.mEntries[i];
      if (e == token) {
        ++mFreed;
        if (--bkt.mInUse > i) {
          bkt.mEntries[i] = bkt.mEntries[bkt.mInUse];
        }
        return true;
      }
    }
  }
  return false;
}

