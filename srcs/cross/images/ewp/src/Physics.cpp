#include "Physics.h"
#include "FastT2.h" // for oneIn, between, etc
#include "HostBlock.h"
#include "FastLocal.h" // for fAll
#include "NativeFB.h"  // native FB1/FB4, shared with the host test
#include "BytePrograms.h" // bytecode FB1/FB4 (scripts/bytephys)
#include "ByteBench.h"    // per-opcode microbenchmark (benchgen.py)
#include "Printf.h"    // for snprintf

namespace MFM {

  extern HostBlock theHostBlock;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  static constexpr u32 PHY_BOOM = 6u;   // self-test: scribble, then FAIL

  // Bytecode spike (OPEN_ISSUES 'physics as bytecode'): FB1 and FB4 run
  // by the BytePhys machine, so 'decay 7' vs 'decay 4' compares them.
  // Every compiled type runs from the default case (programFor); these
  // names are for timing slots and the checks below.
  static constexpr u32 PHY_BFB1 = 7u;
  static constexpr u32 PHY_BFB4 = 8u;
  static constexpr u32 BP_FUEL = 10'000u; // instructions per event
  // ulamlite.py numbers compiled elements from BP_FIRST_TYPE, sorted by
  // name; these two are still seeded and timed by number, so check them
  static_assert(BytePhys::BP_FIRST_TYPE > PHY_BOOM, "compiled types "
                "must not collide with the native ones");
  static_assert(PHY_BFB1 == BytePhys::BP_TYPE_BFB1,
                "ulamlite renumbered BFB1: update PHY_BFB1");
  static_assert(PHY_BFB4 == BytePhys::BP_TYPE_BFB4,
                "ulamlite renumbered BFB4: update PHY_BFB4");

  // BytePhys per-opcode microbenchmark (scripts/bytephys/benchgen.py,
  // BYTEPHYS.md section 10): `seed 3 31` puts Bench atoms down; each
  // event on one runs the next bench program (runBench, `.pb` marks),
  // BENCH_ROUNDS times through the list, then the atom erases itself.
  static constexpr u32 PHY_BENCH = 31u;
  static constexpr u32 BENCH_ROUNDS = 4u;
  static constexpr u32 BENCH_PROBE_LOADS = 64u;
  static_assert(PHY_BENCH == BytePhys::Bench::BP_TYPE_BENCH,
                "benchgen.py BENCH_TYPE and PHY_BENCH disagree");
  static_assert(BytePhys::BP_FIRST_TYPE + BytePhys::BP_TYPE_COUNT
                <= PHY_BENCH, "compiled types reach PHY_BENCH");

  // The tile's wall clock, AI clock cycles (as FastT0.cpp reads it); the
  // low word is enough for one event.
  static const volatile u32 * PT_WALL_CLOCK_LO =
    (volatile u32 *) 0xffb1'21f0u;
  /* Small enough that a sparse phase still logs often (and see
     OPEN_ISSUES 'Log car dies after 65.5 s of silence': a quiet EWP's
     log path dies, so these marks may then reach the host only through
     slowscan's HostBlock ring dump). */
  static constexpr u32 PT_EVENTS_PER_MARK = 1u << 12;

  /* What NativeFB.h and BytePhys.h need from the EWP. */
  struct EwpPhysEnv {
    EventWindow & mEW;

    P4Atom & atom(u32 sn) { return mEW.mAtoms[sn]; }
    u32 between(u32 lo, u32 hi) { return MFM::between(lo, hi); }
    bool oneIn(u32 n) { return MFM::oneIn(n); }
    void splat() { EACH(1,LOGPTAG(SPLAT!,__EACHNUM__)); }

    /* A bytecode machine fault: FAIL into the physics RecoverPoint (one
       FAIL line per kind, so the `.pw` mark's line number names it). */
    [[noreturn]] void fault(u32 f) {
      switch (f) {
      case BytePhys::BF_STACK_OVER: FAIL(OUT_OF_ROOM);
      case BytePhys::BF_STACK_UNDER: FAIL(ILLEGAL_STATE);
      case BytePhys::BF_CALL_OVER: FAIL(OUT_OF_RESOURCES);
      case BytePhys::BF_CALL_UNDER: FAIL(INCONSISTENT_STATE);
      case BytePhys::BF_OPCODE: FAIL(UNSUPPORTED_OPERATION);
      case BytePhys::BF_PC: FAIL(OUT_OF_BOUNDS);
      case BytePhys::BF_SITE: FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
      case BytePhys::BF_FIELD: FAIL(BAD_VALUE);
      case BytePhys::BF_FUEL: FAIL(OUT_OF_RESOURCES);
      case BytePhys::BF_MEMBER: FAIL(NOT_FOUND);
      case BytePhys::BF_TYPE: FAIL(BAD_CAST);
      case BytePhys::BF_FRAME: FAIL(OUT_OF_ROOM);
      case BytePhys::BF_LOCAL: FAIL(ARRAY_INDEX_OUT_OF_BOUNDS);
      default: FAIL(ILLEGAL_ARGUMENT);
      }
    }
  };

  /* Slot of a timed type, or PT_SLOTS if not timed. */
  static u32 ptSlot(u32 type) {
    return type == PHY_FB1 ? 0u
         : type == PHY_FB4 ? 1u
         : type == PHY_BFB1 ? 2u
         : type == PHY_BFB4 ? 3u
         : (u32) Physics::PT_SLOTS;
  }

  /* `.pt:TYPE:EVENTS:TICKS:INSNS.` (hex) per EWP and timed type, every
     PT_EVENTS_PER_MARK events of that type (or before TICKS could
     overflow): TICKS = AI clock cycles spent in guardedPhysics, INSNS =
     bytecode instructions (0 for native).  runsum.py sums them over all
     tiles: mean cycles per event, native vs bytecode. */
  void Physics::notePhysicsTime(u32 type, u32 ticks) {
    const u32 s = ptSlot(type);
    if (s >= PT_SLOTS) return;
    ++mPtEvents[s];
    mPtTicks[s] += ticks;
    if (s >= 2u) mPtInsns[s] += mVM.mExecuted;
    if (mPtEvents[s] >= PT_EVENTS_PER_MARK || mPtTicks[s] >= (1u << 31)) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%x:%x:%x:%x.", (unsigned) type,
               (unsigned) mPtEvents[s], (unsigned) mPtTicks[s],
               (unsigned) mPtInsns[s]);
      LOGPTAG(.pt,buf);
      HBPTAG(.pt,buf);
      mPtEvents[s] = 0u;
      mPtTicks[s] = 0u;
      mPtInsns[s] = 0u;
    }
  }

  /* Every 100,000 events (hex, LOG and HB):
       `.bc:MISSES:SCANS:REFRESHES:BYTES.`  the chunk cache since the
         last mark (ByteCache.h; SCANS = hits the slot hint missed, found
         by scanning the tags; a hint hit counts nothing, so there is no
         hit count: read misses and scans per 100,000 events); compiled
         types and the bench's F runs use it
       `.sw:USED:HEADROOM.`  hart B's stack high-water mark and the
         untouched canary left above the fast-RAM data, in bytes (the
         whole free stack is canary-filled at boot, _BUD.S; a scan of
         up to ~7 KB, so not every event).  For sizing the cache. */
  void Physics::noteCacheAndStack() {
    char buf[64];
    snprintf(buf, sizeof(buf), "%x:%x:%x:%x.",
             (unsigned) mCodeCache.mMisses,
             (unsigned) mCodeCache.mScans,
             (unsigned) mCodeCache.mRefreshes,
             (unsigned) mCodeCache.mBytesCopied);
    LOGPTAG(.bc,buf);
    HBPTAG(.bc,buf);
    mCodeCache.clearCounts();
    snprintf(buf, sizeof(buf), "%x:%x.", (unsigned) estimateStackUsage(),
             (unsigned) estimateStackHeadroom());
    LOGPTAG(.sw,buf);
    HBPTAG(.sw,buf);
  }

  /* `.pb:NAME:V:TICKS:INSNS.` (TICKS, INSNS hex): one timed run of a
     bench program, V = L (code in L1, as the real physics runs) or F
     (code copied into fast RAM), or a native probe (V = N, INSNS = the
     number of loads).  runsum.py's 'bytecode microbenchmark' section. */
  static void benchMarkOut(const char * name, char v, u32 ticks, u32 n) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s:%c:%x:%x.", name, v, (unsigned) ticks,
             (unsigned) n);
    LOGPTAG(.pb,buf);
    HBPTAG(.pb,buf);
  }

  template <class Env, class Res>
  __attribute__((noinline))
  static void benchRun(BytePhys::Machine & vm, const char * name, char v,
                       const BytePhys::Program & p, Env & env, Res & res) {
    const u32 t0 = *PT_WALL_CLOCK_LO;
    vm.run(p, env, BP_FUEL, res);
    const u32 ticks = *PT_WALL_CLOCK_LO - t0;
    benchMarkOut(name, v, ticks, vm.mExecuted);
  }

  /* BENCH_PROBE_LOADS byte loads from p (16 bytes, round and round):
     the raw cost of a load from wherever p points, loop included. */
  __attribute__((noinline))
  static u32 benchProbe(const volatile u8 * p) {
    u32 s = 0u;
    for (u32 k = 0u; k < BENCH_PROBE_LOADS; ++k) s += p[k & 15u];
    return s;
  }

  static void benchProbeRun(const char * name, const volatile u8 * p) {
    const u32 t0 = *PT_WALL_CLOCK_LO;
    (void) benchProbe(p);
    const u32 ticks = *PT_WALL_CLOCK_LO - t0;
    benchMarkOut(name, 'N', ticks, BENCH_PROBE_LOADS);
  }

  __attribute__((noinline))
  bool Physics::runBench() {
    namespace BB = BytePhys::Bench;
    P4Atom & ca = mFastEW.mAtoms[0];
    const u32 idx = ca.mData0;          // Bench.idx (bits 64..79)
    if (idx >= BENCH_ROUNDS * BB::BP_PROG_COUNT) {
      ca = P4Atom::makeEmptyAtom();     // done: leave no trace
      return true;
    }
    const u32 i = idx % BB::BP_PROG_COUNT;
    const BytePhys::Program & p = *BB::BP_PROG_LIST[i];
    const char * name = BB::BP_PROG_NAMES[i] + 4; // drop the "bNN_"
    EwpPhysEnv env{mFastEW};
    BytePhys::InPlace inPlace;
    benchRun(mVM, name, 'L', p, env, inPlace);  // code in L1
    // F: through the code cache (fast RAM), as compiled physics runs; one
    // untimed run first so the timed one starts with its chunks resident
    mVM.run(p, env, BP_FUEL, mCodeCache);
    benchRun(mVM, name, 'F', p, env, mCodeCache);
    if (i == 0u) {
      const BytePhys::Image & img = *p.mImage;
      const u32 c = img.mFuncChunk[p.mFunc];
      if (BytePhys::chunkLength(img, c) >= 16u) {
        benchProbeRun("ldL1", img.mCode + img.mChunkStart[c]);
        mCodeCache.bind(img);
        benchProbeRun("ldfast", mCodeCache.chunk(c));
      }
    }
    ca.mData0 = (u16) (idx + 1u);       // the programs never touch it
    return true;
  }

  bool Physics::doTransition(EwpPayload & ewt) {
    bool ret = false;
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_ONLY) {
      ++mEWsAttempted;
      ewL1ToFast(ewt.mOld);
      const u32 ptType = mFastEW.mAtoms[0].getType();
      const u32 ptStart = *PT_WALL_CLOCK_LO;
      GuardedResult gr = guardedPhysics();
      notePhysicsTime(ptType, *PT_WALL_CLOCK_LO - ptStart);
      if (gr == GR_TRUE) {
        ret = true;
        ewFastToL1(ewt.mNew);
        ++mEWsSucceeded;
      } else {
        // Physics said no, or failed part way: commit the PRE-event window
        // (mOld is never written), so a half-modified mFastEW never lands.
        memcpy((void*) &ewt.mNew, (const void*) &ewt.mOld, sizeof(EventWindow));
        ++mEWsFailed;
        if (gr == GR_JUMPED) {
          u32 cat = ewt.mOld.mAtoms[0].getType();
          // The MFM response to a failing event: erase the center and leave
          // the rest of the window as it was.
          ewt.mNew.mAtoms[0] = P4Atom::makeEmptyAtom();
          ++mEWsPhysFailed;
          // A failure with the stack at (or within 64 bytes of) its limit may
          // be a symptom of stack corruption, not a physics bug: do not
          // recover from that.  (Checked on recovery only: rare.)
          if (estimateStackHeadroom() < 64u) ENGINE_FAIL(OUT_OF_ROOM);
          if (mEWsPhysFailed <= 8u || (mEWsPhysFailed & 1023u) == 0u) {
            // (code+100) is FailCodes' enum value counted from DUMMY, then
            // the center type in the low 9 bits.
            u32 pf = (((u32) (mRecover.mCode + 100)) << 9) | cat;
            u32 pw = (((u32) mRecover.mFileId) << 16) | mRecover.mLine;
            LOGPTAG(.pf,pf);
            HBPTAG(.pf,pf);
            LOGPTAG(.pw,pw);    // where: (file id << 16) | line
            HBPTAG(.pw,pw);
            // Stack high-water mark (bytes) and headroom left above the
            // data (the whole free stack is canary-filled at boot, _BUD.S).
            u32 sk = estimateStackUsage();
            u32 sh = estimateStackHeadroom();
            LOGPTAG(.sk,sk);
            HBPTAG(.sk,sk);
            LOGPTAG(.sh,sh);
            HBPTAG(.sh,sh);
            HBPTAG(PHYSFAIL,mEWsPhysFailed);
          }
        }
      }
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_AND_DEST;

      // between events (no run() active): re-copy one code cache slot
      // from L1 now and then (ByteCache.h refreshOne; flaky memory)
      if ((mEWsSincePew & (CODE_REFRESH_EVENTS - 1u)) == 0u)
        mCodeCache.refreshOne();

      if (++mEWsSincePew == 100'000u) { // every 100,000th attempt, as
        mEWsSincePew = 0u;                // mEWsAttempted % 100'000 was
        HBPTAG(pewAt,mEWsAttempted);
        HBPTAG(pewSu,mEWsSucceeded);
        HBPTAG(pewPF,mEWsPhysFailed);
        noteCacheAndStack();
      }
    } else if (ewt.mPayloadState.mPayloadCode != EwpPayloadCode::EWPC_EMPTY) {
      HBPTAG(wotwot?,ewt.mPayloadState.mPayloadCode);
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_EMPTY;
    } 
    return ret;
  }

  /* The one setjmp per event.  Small and noinline on purpose: HB stack is
     precious, and this frame is all the guard costs.  Nothing here is read
     after the jump except `this` (unmodified, so it survives in a callee-saved
     register) and members. */
  __attribute__((noinline))
  Physics::GuardedResult Physics::guardedPhysics() {
    if (RECOVER_SETJMP(mRecover.mJB) == 0) {
      fAll.mRecover = &mRecover;        // FAILs on THIS hart now land here
      bool r = doFastPhysics();
      fAll.mRecover = 0;
      return r ? GR_TRUE : GR_FALSE;
    }
    return GR_JUMPED;           // PrepareToDie cleared fAll.mRecover, set mCode
  }

  bool Physics::doFastPhysics() {
    HostBlock & hb = theHostBlock;
    EventWindow & ew = mFastEW;
    P4Atom & ca = ew.mAtoms[0];
    u32 cat = ca.getType();

    SNAP(50,HBXTAG(FIZIX,cat));

    ////// BEGIN "PHYSICS" //////
    switch (cat) {
    default: {
      // a compiled (bytecode) type?  scripts/ulamlite assigns these
      // numbers; BytePrograms.h maps them to programs
      if (const BytePhys::Program * p = BytePhys::programFor(cat)) {
        EwpPhysEnv env{ew};
        return mVM.run(*p, env, BP_FUEL, mCodeCache); // chunks in fast RAM
      }
      // wth are you? 
      ca = P4Atom::makeEmptyAtom(); // buhbye now
      return true;
    }
    case P4Atom::EMPTY_TYPE: return true;
    case PHY_BENCH: return runBench();
    case P4Atom::INACCESSIBLE_TYPE:
      // The hub must never dispatch an event centered on a site that does
      // not exist: the engine is broken, so ENGINE_FAIL (never recoverable).
      ENGINE_FAIL(ILLEGAL_STATE);

    case P4Atom::START_TYPE: {
      //DP.printf("IAMI P4Atom::START_TYPE@0x%x\n",(u32) &ca);
      if (hb.mCommonArgs[1] != U16_MAX)
        ca = P4Atom::makeAtom((u16) hb.mCommonArgs[1]);
      else 
        ca = P4Atom::makeAtom(PHY_DREG);
      HBXTAG(STARTZO,ca.getType());
      return true;
    }

    case PHY_FB1: {             // native (NativeFB.h)
      EwpPhysEnv env{ew};
      return nativeFB1(env);
    }

    case PHY_FB4: {             // native (NativeFB.h)
      EwpPhysEnv env{ew};
      const bool r = nativeFB4(env);
      EACH(1'000'000,HBPTAG(FB4,ca.mStg[1]));
      return r;
    }

    case PHY_DREG: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      u32 natype = na.getType();
      if (natype == P4Atom::INACCESSIBLE_TYPE) {
        // DO NOTHING! DON'T GO THERE!
      } else if (natype == P4Atom::EMPTY_TYPE) {
        if (oneIn(250u)) na = ca; // New me!
        else if (oneIn(25u)) na = P4Atom::makeAtom(PHY_RES);
        else ew.swap(0u,ngbsn);
      } else if (natype == PHY_DREG) { // dreg on dreg battle
        if (oneIn(20u)) na = P4Atom::makeEmptyAtom();
      } else if (oneIn(40u)) { // general destruction
        na = P4Atom::makeEmptyAtom();
        if (!ew.swap(0u,ngbsn))
          FAIL(USER_REQUESTED_FAILURE); // XXX use swap retval
      } 
      return true;
    }
      
    case PHY_BOOM: {
      // Self-test for physics-failure recovery (seed with 'decay 6'): spread
      // over the neighbors, then fail.  Recovery must commit the OLD window
      // with this center erased, so the neighbors show no trace of it.
      for (u32 ngbsn = 1u; ngbsn <= 4u; ++ngbsn) ew.mAtoms[ngbsn] = ca;
      FAIL(USER_REQUESTED_FAILURE);
    }

    case PHY_RES: {
      u32 ngbsn = between(1u, 4u);
      P4Atom & na = ew.mAtoms[ngbsn];
      if (na.getType() == P4Atom::EMPTY_TYPE)
        ew.swap(0u,ngbsn);
      return true;
    }
    }
    //// END OF "PHYSICS" ////
    
    return false; // NOT REACHED
  }
}
