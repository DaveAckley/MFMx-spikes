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
   itself.  Machine::run binds the cache to its image once, then asks
   for a chunk only at its entry and at cross-chunk jumps, calls and
   returns:

     bind(img)  the image the chunks come from; a different image than
                last time empties the cache (one physics at a time:
                which one may be chosen at start, BYTEPHYS.md)
     chunk(c)   the address of chunk c's bytes in a fast-RAM slot

   SLOTS slots of CHUNK_MAX bytes, fully associative, each tagged with
   the chunk it holds.  A HIT is two loads and a compare: mSlotOf, a
   hint table indexed by the chunk number's low bits, names the slot
   that chunk was last put in, and that slot's tag confirms it.  A hit
   writes nothing (no counters, no use stamps).  Otherwise (the hint was
   overwritten by another chunk with the same low bits, or the slot was
   reused) the tags are scanned, out of line; a chunk in no slot is a
   MISS, copied from L1 into the next slot in turn (FIFO: hits leave no
   record to choose by).  A copy of a verified chunk is the same verified
   chunk (BytePhys.h 'VERIFY ONCE').

   Belt and suspenders for flaky memory: refreshOne() re-copies one slot
   from L1, the slots in turn.  The EWP calls it every REFRESH_EVENTS
   events, between events (no run() active), so it costs the hit path
   nothing.  And run()'s dispatch masks each opcode to its table, so even
   a corrupt byte reaches a handler or BF_OPCODE, never a wild jump.

   On the EWP the cache is Physics::mCodeCache (hart B fast RAM; budget
   ~3 KB, BYTEPHYS.md: 12 slots); initB resets it.  If small functions
   waste their slots, the allocator can move to packed 64-byte units
   without touching the format or run(). */

#include "itype.h"
#include "BytePhys.h"

namespace MFM {
  namespace BytePhys {

    template <u32 SLOTS, u32 SLOT_BYTES = CHUNK_MAX, u32 HINTS = 64>
    struct CodeCache {
      enum : u32 {
        SLOT_COUNT = SLOTS,
        SLOT_SIZE = SLOT_BYTES,
        HINT_COUNT = HINTS,
        NO_CHUNK = 0xffffu,             //< the tag of an empty slot
      };
      static_assert(SLOTS > 0u && SLOTS <= 256u, "slot numbers are u8");
      static_assert(SLOT_BYTES >= CHUNK_MAX, "a slot holds any chunk");
      static_assert(SLOT_BYTES % 4u == 0u, "slots are word sized");
      static_assert(HINTS > 0u && (HINTS & (HINTS - 1u)) == 0u,
                    "the hint index is a mask");
      // (a chunk number is < mChunkCount <= 0xffff, never NO_CHUNK)

      u8 mCode[SLOTS][SLOT_BYTES] __attribute__((aligned(4)));
      u16 mTag[SLOTS];                  //< chunk held, or NO_CHUNK
      u8 mSlotOf[HINTS];                //< chunk & (HINTS-1) -> a slot
      const Image * mImage;             //< bound image, or null
      u32 mNext;                        //< the slot the next miss fills
      u32 mNextRefresh;                 //< the slot refreshOne copies
      u32 mMisses;
      u32 mScans;                       //< hits found by a scan
      u32 mRefreshes;                   //< slots re-copied (refreshOne)
      u32 mBytesCopied;

      /* Empty every slot, unbind, zero the counters.  The EWP calls this
         in initB; a host cache must be value-initialized or reset. */
      void reset() {
        mImage = nullptr;
        empty();
        for (u32 i = 0u; i < HINTS; ++i) mSlotOf[i] = 0u;
        mNextRefresh = 0u;
        clearCounts();
      }

      void clearCounts() {
        mMisses = 0u;
        mScans = 0u;
        mRefreshes = 0u;
        mBytesCopied = 0u;
      }

      /* Machine::run's resolver, part 1: once per run. */
      void bind(const Image & img) {
        if (__builtin_expect(&img != mImage, 0)) {
          empty();
          mImage = &img;
        }
      }

      /* Part 2: where chunk c of the bound image is now. */
      const u8 * chunk(u32 c) {
        const u32 s = mSlotOf[c & (HINTS - 1u)];
        if (__builtin_expect(mTag[s] == c, 1)) return mCode[s];
        return findOrLoad(c);
      }

      /* Re-copy the next slot in turn from the bound image (a slot of
         nothing is skipped).  Never during a run(). */
      void refreshOne() {
        const u32 s = mNextRefresh;
        mNextRefresh = s + 1u == SLOTS ? 0u : s + 1u;
        if (mImage != nullptr && mTag[s] != NO_CHUNK) {
          copyInto(s, mTag[s]);
          ++mRefreshes;
        }
      }

    private:
      void empty() {
        for (u32 i = 0u; i < SLOTS; ++i) mTag[i] = (u16) NO_CHUNK;
        mNext = 0u;
      }

      __attribute__((noinline, cold))
      const u8 * findOrLoad(u32 c) {
        for (u32 i = 0u; i < SLOTS; ++i) {
          if (mTag[i] == c) {
            mSlotOf[c & (HINTS - 1u)] = (u8) i;
            ++mScans;
            return mCode[i];
          }
        }
        const u32 s = mNext;
        mNext = s + 1u == SLOTS ? 0u : s + 1u;
        copyInto(s, c);
        mSlotOf[c & (HINTS - 1u)] = (u8) s;
        ++mMisses;
        return mCode[s];
      }

      void copyInto(u32 s, u32 c) {
        const Image & img = *mImage;
        const u32 n = chunkLength(img, c);
        __builtin_memcpy(mCode[s], img.mCode + img.mChunkStart[c], n);
        mTag[s] = (u16) c;
        mBytesCopied += n;
      }
    };

  }
}
