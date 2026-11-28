#pragma once  /* -*- C++ -*- */
/* ByteCache.h -- a cache of bytecode CHUNKS in fast RAM (2026-11-27).

   Manual: BYTEPHYS.md section 12 ('Code cache').

   Every BytePhys dispatch loads its opcode byte from wherever the code
   lives.  The generated images (BytePrograms.h) are constexpr arrays in
   L1, where a load costs 8+ cycles (the L0 data cache is off in every
   MFMx image) against 2 in the hart's local fast RAM; whole programs
   copied to fast RAM made FB4 22% faster (run 20261127-200923).  All of
   a real physics will not fit, so the unit cached is the CHUNK
   (BytePhys.h 'IMAGES, FUNCTIONS, CHUNKS'): a function, or a piece of
   one, of at most CHUNK_MAX bytes, whose jumps are all relative to
   itself.  Machine::run asks for a chunk only at its entry and at
   cross-chunk jumps, calls and returns:

     chunk(img, c)  the address of chunk c's bytes: a fast-RAM slot,
                    copied on a miss into the least recently used one

   Fixed slots of CHUNK_MAX bytes, fully associative (a chunk may sit in
   any slot), keyed by (image, chunk).  The most recently used slot is
   tried first, which is most transfers (a loop's own chunk, a return
   into the caller just left); otherwise a scan of the tags.  A copy of
   a verified chunk is the same verified chunk (BytePhys.h 'VERIFY
   ONCE').  Belt and suspenders for flaky memory: a slot is copied afresh
   from L1 every REFRESH_USES lookups that hit it, and run()'s dispatch
   masks each opcode to its table, so even a corrupt byte reaches a
   handler or BF_OPCODE, never a wild jump.

   On the EWP the cache is Physics::mCodeCache (hart B fast RAM; budget
   ~3 KB, BYTEPHYS.md: 12 slots); initB resets it.  If small functions
   waste their slots, the allocator can move to packed 64-byte units
   without touching the format or run(). */

#include "itype.h"
#include "BytePhys.h"

namespace MFM {
  namespace BytePhys {

    template <u32 SLOTS, u32 SLOT_BYTES = CHUNK_MAX>
    struct CodeCache {
      enum : u32 {
        SLOT_COUNT = SLOTS,
        SLOT_SIZE = SLOT_BYTES,
        REFRESH_USES = 4096,            //< re-copy a slot this often
      };
      static_assert(SLOTS > 0u, "a cache needs a slot");
      static_assert(SLOT_BYTES >= CHUNK_MAX, "a slot holds any chunk");
      static_assert(SLOT_BYTES % 4u == 0u, "slots are word sized");

      struct Slot {
        const Image * mImage;           //< whose chunk, or null
        u32 mLastUse;                   //< mClock at the last use (LRU)
        u16 mChunk;
        u16 mUses;                      //< hits since the last copy
        u8 mCode[SLOT_BYTES];
      };

      Slot mSlots[SLOTS];
      u32 mClock;                       //< counts lookups
      u32 mMru;                         //< the slot used last
      u32 mHits;
      u32 mMisses;
      u32 mScans;                       //< hits not in the MRU slot
      u32 mRefreshes;                   //< slots re-copied (REFRESH_USES)
      u32 mBytesCopied;

      /* Empty every slot and zero the counters.  The EWP calls this in
         initB; a host cache must be value-initialized or reset. */
      void reset() {
        for (u32 i = 0u; i < SLOTS; ++i) {
          mSlots[i].mImage = nullptr;
          mSlots[i].mLastUse = 0u;
          mSlots[i].mChunk = 0u;
          mSlots[i].mUses = 0u;
        }
        mClock = 0u;
        mMru = 0u;
        clearCounts();
      }

      void clearCounts() {
        mHits = 0u;
        mMisses = 0u;
        mScans = 0u;
        mRefreshes = 0u;
        mBytesCopied = 0u;
      }

      /* Where chunk c of img is now (Machine::run's resolver). */
      const u8 * chunk(const Image & img, u32 c) {
        ++mClock;
        Slot * s = &mSlots[mMru];
        if (s->mImage != &img || s->mChunk != c) {
          s = nullptr;
          for (u32 i = 0u; i < SLOTS; ++i) {
            if (mSlots[i].mImage == &img && mSlots[i].mChunk == c) {
              s = &mSlots[i];
              mMru = i;
              ++mScans;
              break;
            }
          }
        }
        if (s != nullptr) {
          ++mHits;
          if (++s->mUses >= REFRESH_USES) {     // bounded life in fast RAM
            copyInto(*s, img, c);
            ++mRefreshes;
          }
        } else {
          ++mMisses;
          u32 v = 0u;                           // LRU (empty slots first)
          for (u32 i = 1u; i < SLOTS; ++i) {
            if (mSlots[v].mImage == nullptr) break;
            if (mSlots[i].mImage == nullptr ||
                mSlots[i].mLastUse < mSlots[v].mLastUse) v = i;
          }
          s = &mSlots[v];
          mMru = v;
          copyInto(*s, img, c);
        }
        s->mLastUse = mClock;
        return s->mCode;
      }

    private:
      void copyInto(Slot & s, const Image & img, u32 c) {
        const u32 n = chunkLength(img, c);
        __builtin_memcpy(s.mCode, img.mCode + img.mChunkStart[c], n);
        s.mImage = &img;
        s.mChunk = (u16) c;
        s.mUses = 0u;
        mBytesCopied += n;
      }
    };

  }
}
