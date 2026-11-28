#pragma once  /* -*- C++ -*- */
/* ByteCache.h -- a cache of bytecode programs in fast RAM (2026-11-27).

   Manual: BYTEPHYS.md section 12 ('Code cache').

   Every BytePhys dispatch loads its opcode byte from wherever the
   program lives.  The generated programs (BytePrograms.h) are constexpr
   arrays in L1, where a load costs 8+ cycles (the L0 data cache is off
   in every MFMx image) against 2 in the hart's local fast RAM; the
   microbenchmark measured ~6-7 cycles per ordinary instruction and ~13
   per taken jump between code in L1 and code in fast RAM (run
   20261127-151122).  All of a real physics' bytecode will not fit in
   fast RAM, so this keeps the programs in use in a few fixed slots:

     use(p)  returns the Program to run: p with mCode pointing at a
             fast-RAM copy (copied on a miss into the least recently
             used slot), or p itself if it is longer than a slot
             (counted as tooBig; it runs from L1 as before)

   Whole programs, keyed by their code pointer; the generated programs
   are static, so the pointer is stable.  A copy of a verified program
   is the same verified program, so run() may trust it like the
   original (BytePhys.h 'VERIFY ONCE').  Belt and suspenders for flaky
   memory: a slot is copied afresh from L1 every REFRESH_USES uses
   (counted as refreshes), so a flipped bit in fast RAM lives a bounded
   time; and run()'s dispatch masks the opcode to its table size, so even
   a corrupt opcode byte can only reach a handler or BF_OPCODE.

   On the EWP the cache is Physics::mCodeCache (hart B fast RAM, beside
   the Machine); initB resets it.  Function-granular caching (for
   programs bigger than a slot and calls across elements) is the next
   step (OPEN_ISSUES 'physics as bytecode'). */

#include "itype.h"
#include "BytePhys.h"

namespace MFM {
  namespace BytePhys {

    template <u32 SLOTS, u32 SLOT_BYTES>
    struct CodeCache {
      enum : u32 {
        SLOT_COUNT = SLOTS,
        SLOT_SIZE = SLOT_BYTES,
        REFRESH_USES = 4096,            //< re-copy a slot this often
      };
      static_assert(SLOTS > 0u, "a cache needs a slot");
      static_assert(SLOT_BYTES % 4u == 0u, "slots are word sized");

      struct Slot {
        const u8 * mFrom;               //< the L1 code copied, or null
        u32 mLastUse;                   //< mClock at the last use (LRU)
        u16 mUses;                      //< uses since the last copy
        u16 mLength;                    //< bytes copied
        u8 mCode[SLOT_BYTES];
      };

      Slot mSlots[SLOTS];
      u32 mClock;                       //< counts use() calls
      u32 mHits;
      u32 mMisses;
      u32 mTooBig;                      //< ran from L1: bigger than a slot
      u32 mRefreshes;                   //< slots re-copied (REFRESH_USES)
      u32 mBytesCopied;

      /* Empty every slot and zero the counters.  The EWP calls this in
         initB; a host cache must be value-initialized or reset. */
      void reset() {
        for (u32 i = 0u; i < SLOTS; ++i) {
          mSlots[i].mFrom = nullptr;
          mSlots[i].mLastUse = 0u;
          mSlots[i].mUses = 0u;
          mSlots[i].mLength = 0u;
        }
        mClock = 0u;
        clearCounts();
      }

      void clearCounts() {
        mHits = 0u;
        mMisses = 0u;
        mTooBig = 0u;
        mRefreshes = 0u;
        mBytesCopied = 0u;
      }

      /* The program to run in place of p (see the header comment). */
      Program use(const Program & p) {
        ++mClock;
        if (p.mLength > SLOT_BYTES) {
          ++mTooBig;
          return p;
        }
        Slot * s = nullptr;
        for (u32 i = 0u; i < SLOTS; ++i) {
          if (mSlots[i].mFrom == p.mCode && mSlots[i].mLength == p.mLength) {
            s = &mSlots[i];
            break;
          }
        }
        if (s != nullptr) {
          ++mHits;
          if (++s->mUses >= REFRESH_USES) {     // bounded life in fast RAM
            copyInto(*s, p);
            ++mRefreshes;
          }
        } else {
          ++mMisses;
          s = &mSlots[0];                       // LRU (empty slots first)
          for (u32 i = 1u; i < SLOTS; ++i) {
            const Slot & t = mSlots[i];
            if (s->mFrom == nullptr) break;
            if (t.mFrom == nullptr || t.mLastUse < s->mLastUse) s = &mSlots[i];
          }
          copyInto(*s, p);
        }
        s->mLastUse = mClock;
        Program q = p;
        q.mCode = s->mCode;
        return q;
      }

    private:
      void copyInto(Slot & s, const Program & p) {
        __builtin_memcpy(s.mCode, p.mCode, p.mLength);
        s.mFrom = p.mCode;
        s.mLength = p.mLength;
        s.mUses = 0u;
        mBytesCopied += p.mLength;
      }
    };

  }
}
