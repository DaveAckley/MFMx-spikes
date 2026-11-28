// bytephys_test.cpp -- native test of the bytecode physics spike:
// srcs/shared/include/BytePhys.h (the machine), BytePrograms.h (BFB1 and
// BFB4, compiled from scripts/ulamlite/*.ulam by ulamlite.py, assembled
// by scripts/bytephys/bpasm.py) against NativeFB.h (the EWP's native
// FB1/FB4, the very same code Physics.cpp runs).
// Build/run: see the bytephys_test line of run_native_tests.sh.
//
// Checks:
//   * bytecode FB1/FB4 leave EXACTLY the window native FB1/FB4 leave, for
//     many random windows and counter states, under several random
//     streams (uniform, always-low, always-high), drawing the same
//     numbers in the same order
//     (counters kept clear of 2^32, where native wraps and bytecode
//     saturates; saturation is checked on its own)
//   * every machine fault is caught: stack under/overflow, call
//     under/overflow (runaway recursion), bad opcode, jump past the end,
//     running off the end, bad site, bad/read-only field, fuel, bad
//     member id, member of the wrong element type
//   * frames: recursion through call/ret with locals (each frame's
//     locals survive its callees), storel/loadl, enter's argument move,
//     and the frame faults
//   * the compiler: test-only ulam-lite programs (scripts/ulamlite/tests)
//     for what FB1/FB4 do not use -- value methods, recursion, narrow
//     and Bool params/returns/locals, while/break/continue, || ! and
//     comparisons as values, shadowing, is, ew copies, short-circuit
//     draws -- each checked against its hand-computed results
//   * the microbenchmark programs (ByteBench.h): each commits without a
//     fault, leaves every site but the center and the center's idx
//     (mData0) alone, and runs base + a whole number of pattern copies
//   * declared members: getBits/setBits against a bit-by-bit model over
//     all 80 state bits, setm saturates to the declared width, add/sub
//     saturate
// and prints the bytecode instructions per event (mean, max).
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <vector>

#include "BytePrograms.h"
#include "BytePhysTests.h"   // test-only physics, scripts/ulamlite/tests
#include "ByteBench.h"       // microbenchmark, scripts/bytephys/benchgen.py
#include "ByteDReg.h"        // DReg + Res, scripts/ulamlite/dreg
#include "NativeFB.h"
#include "ByteCache.h"         // fast-RAM program cache (EWP)

using namespace MFM;

namespace {

int g_bad = 0;

/* The generated BCP blobs, opened once (as the EWP does in initB). */
BytePhys::BCPView openOrDie(const u8 * b, u32 n, const char * what) {
  BytePhys::BCPView v{};
  if (const u32 bad = BytePhys::openBCP(b, n, v)) {
    printf("FAIL: %s does not open (%u)\n", what, bad);
    exit(1);
  }
  return v;
}
const BytePhys::BCPView g_phys =
  openOrDie(BytePhys::BP_BCP, sizeof(BytePhys::BP_BCP), "BP_BCP");
const BytePhys::BCPView g_tests =
  openOrDie(BytePhys::Tests::BP_BCP, sizeof(BytePhys::Tests::BP_BCP),
            "Tests::BP_BCP");
const BytePhys::BCPView g_bench =
  openOrDie(BytePhys::Bench::BP_BCP, sizeof(BytePhys::Bench::BP_BCP),
            "Bench::BP_BCP");
const BytePhys::BCPView g_dreg =
  openOrDie(BytePhys::DReg::BP_BCP, sizeof(BytePhys::DReg::BP_BCP),
            "DReg::BP_BCP");
const BytePhys::Program PROG_BFB1 = {&g_phys, BytePhys::BP_FUNC_BFB1};
const BytePhys::Program PROG_BFB4 = {&g_phys, BytePhys::BP_FUNC_BFB4};
BytePhys::Program testProg(u16 f) { return {&g_tests, f}; }
BytePhys::Program benchProg(u16 f) { return {&g_bench, f}; }

void check(bool ok, const char * what) {
  if (!ok) {
    if (g_bad < 20) printf("FAIL: %s\n", what);
    ++g_bad;
  }
}

struct Fault {
  u32 code;
};

/* A deterministic random stream: mode 0 xorshift, 1 always the lowest
   value (oneIn always true), 2 always the highest (oneIn always false). */
struct Env {
  P4Atom mW[BytePhys::SITES];
  u32 mState;
  int mMode;
  std::vector<u32> mDraws;              // every draw, to compare order
  int mSplats;

  Env(u32 seed, int mode) : mState(seed | 1u), mMode(mode), mSplats(0) { }

  u32 create(u32 n) {
    u32 r;
    if (mMode == 1) r = 0u;
    else if (mMode == 2) r = n - 1u;
    else {
      mState ^= mState << 13;
      mState ^= mState >> 17;
      mState ^= mState << 5;
      r = mState % n;
    }
    mDraws.push_back(r);
    return r;
  }

  P4Atom & atom(u32 sn) { return mW[sn]; }
  u32 between(u32 lo, u32 hi) { return create(hi - lo + 1u) + lo; }
  bool oneIn(u32 n) { return create(n) < 1u; }
  void splat() { ++mSplats; }
  [[noreturn]] void fault(u32 f) { throw Fault{f}; }
};

bool sameWindow(const Env & a, const Env & b) {
  return memcmp(a.mW, b.mW, sizeof(a.mW)) == 0;
}

/* centerType: the bytecode programs only accept their own element type
   at the center (getm faults otherwise); native does not look. */
void fillWindow(Env & e, u32 seed, u16 centerType = 4u) {
  u32 s = seed * 2654435761u + 1u;
  for (u32 i = 0; i < BytePhys::SITES; ++i) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    e.mW[i] = P4Atom::makeAtom((u16) (s % 3u == 0u ? 0u : 4u));
    e.mW[i].mData0 = (u16) s;
    e.mW[i].mStg[0] = s % 7u;
    e.mW[i].mStg[1] = s >> 3;
  }
  // center counters that reach every FB1 branch
  const u32 stg0s[] = {0u, 1u, 2u, 3u, 500u};
  const u32 stg1s[] = {0u, 19u, 20u, 21u, 0x7ffffffeu, 0x7fffffffu};
  e.mW[0] = P4Atom::makeAtom(centerType);
  e.mW[0].mStg[0] = stg0s[seed % 5u];
  e.mW[0].mStg[1] = stg1s[(seed / 5u) % 6u];
}

void testEquivalence() {
  BytePhys::Machine vm{};
  u64 insns[2] = {0, 0};
  u32 maxInsns[2] = {0, 0};
  long runs[2] = {0, 0};
  for (int mode = 0; mode < 3; ++mode) {
    for (u32 seed = 1; seed <= 3000; ++seed) {
      for (int fb = 0; fb < 2; ++fb) {
        Env n(seed * 7919u + (u32) mode, mode);
        Env b(seed * 7919u + (u32) mode, mode);
        const u16 ct = fb == 0 ? BytePhys::BP_TYPE_BFB1
                               : BytePhys::BP_TYPE_BFB4;
        fillWindow(n, seed, ct);
        fillWindow(b, seed, ct);
        const bool rn = fb == 0 ? nativeFB1(n) : nativeFB4(n);
        bool rb = false;
        try {
          rb = vm.run(fb == 0 ? PROG_BFB1
                              : PROG_BFB4, b, 10'000u);
        } catch (const Fault & f) {
          check(false, "bytecode FB faulted");
          continue;
        }
        check(rn == rb, "native and bytecode results differ");
        check(sameWindow(n, b), fb == 0 ? "FB1 windows differ"
                                        : "FB4 windows differ");
        check(n.mDraws == b.mDraws, "random draws differ (count or order)");
        insns[fb] += vm.mExecuted;
        maxInsns[fb] = vm.mExecuted > maxInsns[fb] ? vm.mExecuted
                                                   : maxInsns[fb];
        ++runs[fb];
      }
    }
  }
  printf("equivalence: %ld FB1 + %ld FB4 windows identical to native; "
         "bytecode instructions per event FB1 mean %.1f max %u, FB4 mean "
         "%.1f max %u\n", runs[0], runs[1],
         (double) insns[0] / (double) runs[0], maxInsns[0],
         (double) insns[1] / (double) runs[1], maxInsns[1]);
  const BytePhys::BCPView & img = g_phys;
  printf("image: %u bytes, %u chunks, %u functions; machine %zu bytes\n",
         (unsigned) img.mChunkStart[img.mChunkCount],
         (unsigned) img.mChunkCount, (unsigned) img.mFuncCount,
         sizeof(BytePhys::Machine));
}

/* Pack a BCP blob as bpasm.py's pack_bcp does (BytePhys.h 'BCP'), from
   chunks back to back, fchunks[f] = function f's first chunk, a layout
   (or none) and a type table (first type, entry functions). */
std::vector<u32> packBCP(const std::vector<std::vector<u8>> & chunks,
                         const std::vector<u16> & fchunks,
                         const BytePhys::Layout * lay,
                         u16 firstType = 0u,
                         const std::vector<u16> & tfunc = {}) {
  using namespace BytePhys;
  std::vector<u8> body;
  std::vector<u32> offs;
  auto pad = [&]() { while (body.size() % 4u) body.push_back(0u); };
  auto put16 = [&](u32 v) {
    body.push_back((u8) v);
    body.push_back((u8) (v >> 8));
  };
  std::vector<u16> starts;
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  u32 at = 0u;
  for (const auto & c : chunks) {
    starts.push_back((u16) at);
    body.insert(body.end(), c.begin(), c.end());
    at += (u32) c.size();
  }
  starts.push_back((u16) at);
  pad();
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  for (u16 v : starts) put16(v);
  pad();
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  for (u16 v : fchunks) put16(v);
  pad();
  const u32 nrefs = lay ? lay->mRefCount : 0u;
  u32 nats = 0u;
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  for (u32 r = 0; r < nrefs; ++r) {
    put16(lay->mRefs[r].mFirst);
    put16(lay->mRefs[r].mCount);
    nats = std::max(nats, (u32) lay->mRefs[r].mFirst + lay->mRefs[r].mCount);
  }
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  for (u32 a = 0; a < nats; ++a) {
    put16(lay->mAts[a].mType);
    body.push_back(lay->mAts[a].mPos);
    body.push_back(lay->mAts[a].mLen);
  }
  offs.push_back(BCP_HEADER_BYTES + (u32) body.size());
  for (u16 f : tfunc) put16(f);
  pad();
  const u32 size = BCP_HEADER_BYTES + (u32) body.size();
  std::vector<u8> b(BCP_HEADER_BYTES, 0u);
  auto w16 = [&](u32 i, u32 v) {
    b[i] = (u8) v;
    b[i + 1u] = (u8) (v >> 8);
  };
  auto w32 = [&](u32 i, u32 v) {
    w16(i, v & 0xffffu);
    w16(i + 2u, v >> 16);
  };
  w32(BCPH_MAGIC, BCP_MAGIC);
  w16(BCPH_VERSION, BCP_VERSION);
  w16(BCPH_OPCOUNT, OP_COUNT);
  w32(BCPH_SIZE, size);
  w16(BCPH_CHUNKS, (u32) chunks.size());
  w16(BCPH_FUNCS, (u32) fchunks.size());
  w16(BCPH_REFS, nrefs);
  w16(BCPH_ATS, nats);
  w16(BCPH_FIRST_TYPE, firstType);
  w16(BCPH_TYPES, (u32) tfunc.size());
  for (u32 k = 0; k < 6u; ++k) w32(BCPH_OFF_CODE + 4u * k, offs[k]);
  b.insert(b.end(), body.begin(), body.end());
  w32(BCPH_CHECKSUM, bcpChecksum(b.data(), size));
  std::vector<u32> words(size / 4u);    // 4-aligned storage
  memcpy(words.data(), b.data(), size);
  return words;
}

/* A hand-made image, packed and opened; bad = what openBCP said (then
   the image must not run).  Not copyable (img points into blob). */
struct RawImage {
  std::vector<u32> blob;
  BytePhys::BCPView img{};
  BytePhys::Program prog;
  u32 bad;

  RawImage(const std::vector<std::vector<u8>> & chunks,
           const std::vector<u16> & fchunks, const BytePhys::Layout * lay)
    : blob(packBCP(chunks, fchunks, lay)) {
    bad = BytePhys::openBCP((const u8 *) blob.data(),
                            (u32) (blob.size() * 4u), img);
    prog = {&img, 0u};
  }

  RawImage(const std::vector<u8> & one, const BytePhys::Layout * lay)
    : RawImage(std::vector<std::vector<u8>>{one}, std::vector<u16>{0u},
               lay) { }

  RawImage(const RawImage &) = delete;
};

/* A hand-made layout for the member tests: member 0 is 4 bits at state
   bit 3 of type 4, member 1 is 8 bits at 28 (straddles mStg[0]/mStg[1])
   of type 4 and 3 bits at 77 (top of mData0) of type 9. */
const BytePhys::MemberRef T_REFS[2] = {{0u, 1u}, {1u, 2u}};
const BytePhys::MemberAt T_ATS[3] = {{4u, 3u, 4u}, {4u, 28u, 8u},
                                     {9u, 77u, 3u}};
const BytePhys::Layout T_LAYOUT = {T_REFS, 2u, T_ATS};

u32 faultOfImage(const RawImage & ri, u32 fuel = 10'000u,
                 u16 centerType = 4u) {
  BytePhys::Machine vm{};
  const BytePhys::Program & p = ri.prog;
  // what verify() rejects never runs (the EWP's images are verified at
  // compile time): report it as the fault it stands for
  if (ri.bad) return ri.bad;
  Env e(1u, 0);
  fillWindow(e, 1u, centerType);
  try {
    vm.run(p, e, fuel);
  } catch (const Fault & f) {
    return f.code;
  }
  return 0u;
}

u32 faultOf(const std::vector<u8> & code, u32 fuel = 10'000u,
            const BytePhys::Layout * lay = &T_LAYOUT, u16 centerType = 4u) {
  RawImage ri(code, lay);
  return faultOfImage(ri, fuel, centerType);
}

void testFaults() {
  using namespace BytePhys;
  const u8 P8 = OP_PUSH8;
  check(faultOf({OP_DROP, OP_HALT1}) == BF_STACK_UNDER, "stack underflow");
  std::vector<u8> over;
  for (u32 i = 0; i <= Machine::STACK_SIZE; ++i) {
    over.push_back(P8);
    over.push_back(1);
  }
  over.push_back(OP_HALT1);
  check(faultOf(over) == BF_STACK_OVER, "stack overflow");
  check(faultOf({OP_RET}) == BF_CALL_UNDER, "return with no call");
  check(faultOf({OP_CALL, 0, 0, OP_HALT1}) == BF_CALL_OVER,
        "runaway recursion");
  check(faultOf({OP_CALL, 0, 0}) == BF_PC, "call with nothing to return to");
  check(faultOf({OP_JMP, 3, OP_PUSH8, 1, OP_HALT1}) == BF_PC,
        "jump into an immediate");
  check(faultOf({OP_JZ, 0}) == BF_PC, "jz as the last instruction");
  check(faultOf({}) == BF_PC, "empty program");
  check(faultOf({OP_HALT0, 0xee}) == BF_OPCODE,
        "bad opcode where control never goes");
  check(faultOf({0xee}) == BF_OPCODE, "bad opcode");
  check(faultOf({OP_JMP, 9}) == BF_PC, "jump past the end");
  check(faultOf({P8, 1}) == BF_PC, "running off the end");
  check(faultOf({OP_PUSH32, 1}) == BF_PC, "truncated immediate");
  check(faultOf({P8, 41, OP_EMPTY, OP_HALT1}) == BF_SITE, "site 41");
  check(faultOf({P8, 0, P8, 5, OP_SETF, F_TYPE, OP_HALT1}) == BF_FIELD,
        "type is read-only");
  check(faultOf({P8, 0, OP_GETF, 9, OP_HALT1}) == BF_FIELD, "bad field");
  check(faultOf({OP_JMP, 0}, 1000u) == BF_FUEL, "infinite loop");
  check(faultOf({P8, 3, P8, 4, OP_ADD, OP_HALT1}) == 0u, "a good program");
  check(faultOf({P8, 0, OP_GETM, 2, 0, OP_HALT1}) == BF_MEMBER,
        "member id past the layout");
  check(faultOf({P8, 0, OP_GETM, 0, 0, OP_HALT1}, 100u, nullptr)
        == BF_MEMBER, "getm with no layout");
  check(faultOf({P8, 0, OP_GETM, 0, 0, OP_HALT1}, 100u, &T_LAYOUT, 5u)
        == BF_TYPE, "member of another element type");
  check(faultOf({P8, 0, P8, 1, OP_SETM, 0, 0, OP_HALT1}, 100u, &T_LAYOUT,
                9u) == BF_TYPE, "setm on another element type");
  check(faultOf({P8, 41, OP_GETM, 0, 0, OP_HALT1}) == BF_SITE,
        "getm site 41");
  check(faultOf({OP_GETM, 0}) == BF_PC, "truncated member id");
  check(faultOf({P8, 0, OP_GETM, 0, 0, OP_HALT1}) == 0u, "a good getm");
  check(faultOf({OP_LOADL, 0, OP_HALT1}) == BF_LOCAL, "loadl, no frame");
  check(faultOf({OP_ENTER, 0, 2, P8, 1, OP_STOREL, 2, OP_HALT1})
        == BF_LOCAL, "storel past the frame");
  check(faultOf({OP_ENTER, 2, 1, OP_HALT1}) == BF_FRAME, "nargs > nlocals");
  check(faultOf({OP_ENTER, 0, 33, OP_HALT1}) == BF_FRAME,
        "frame bigger than all locals");
  check(faultOf({OP_ENTER, 1, 1, OP_HALT1}) == BF_STACK_UNDER,
        "enter with a missing argument");
  check(faultOf({OP_ENTER, 0, 8, OP_CALL, 0, 0, OP_HALT1}) == BF_FRAME,
        "recursion runs out of locals");
  check(faultOf({OP_ENTER, 1}) == BF_PC, "truncated enter");
  // fuel is compared only at backward jumps and calls: a long straight
  // run may overshoot, a loop may not
  check(faultOf({OP_PUSH8, 1, OP_DROP, OP_PUSH8, 1, OP_DROP, OP_HALT1}, 2u)
        == 0u, "straight-line code is not stopped by fuel");
  check(faultOf({OP_PUSH8, 0, OP_JZ, 0, OP_HALT1}, 50u) == BF_FUEL,
        "a jz loop runs out of fuel");
  // chunks: cross-chunk jumps, calls by function, chunk bounds
  {
    RawImage a({{OP_XJMP, 1, 0, 0}, {OP_HALT1}}, {0u}, nullptr);
    check(faultOfImage(a) == 0u, "xjmp to another chunk");
    RawImage b({{OP_XJMP, 2, 0, 0}, {OP_HALT1}}, {0u}, nullptr);
    check(faultOfImage(b) == BF_PC, "xjmp to no chunk");
    RawImage c({{OP_XJMP, 1, 0, 1}, {OP_PUSH8, 1, OP_HALT1}}, {0u}, nullptr);
    check(faultOfImage(c) == BF_PC, "xjmp into an immediate");
    RawImage d({{OP_XJMP, 1, 0, 0}, {OP_XJMP, 0, 0, 0}}, {0u}, nullptr);
    check(faultOfImage(d, 100u) == BF_FUEL, "a loop across chunks");
    RawImage e({{OP_PUSH8, 0, OP_XJZ, 1, 0, 0, OP_HALT0}, {OP_HALT1}}, {0u},
               nullptr);
    check(faultOfImage(e) == 0u, "xjz taken");
    RawImage f({{OP_CALL, 1, 0, OP_HALT1}, {OP_RET}}, {0u}, nullptr);
    check(faultOfImage(f) == BF_PC, "call to no function");
    RawImage g({{OP_CALL, 1, 0, OP_HALT1}, {OP_RET}}, {0u, 1u}, nullptr);
    check(faultOfImage(g) == 0u, "call another chunk's function and return");
    RawImage h({{OP_HALT1}}, {1u}, nullptr);
    check(faultOfImage(h) == BF_PC, "a function in no chunk");
    // jmp over 253 never-run dups to the halt in the chunk's last byte
    std::vector<u8> big(BytePhys::CHUNK_MAX, OP_DUP);
    big[0] = OP_JMP;
    big[1] = (u8) (BytePhys::CHUNK_MAX - 1u);
    big.back() = OP_HALT1;
    check(faultOf(big) == 0u, "a full chunk");
    big.push_back(OP_HALT1);
    check(faultOf(big) == BF_PC, "a chunk over CHUNK_MAX");
  }
  printf("faults: every machine fault caught (verify() or the run)\n");
}

/* The state bits one at a time: mStg[0] 0..31, mStg[1] 32..63, mData0
   64..79. */
bool stateBit(const P4Atom & a, u32 i) {
  if (i < 32u) return (a.mStg[0] >> i) & 1u;
  if (i < 64u) return (a.mStg[1] >> (i - 32u)) & 1u;
  return (a.mData0 >> (i - 64u)) & 1u;
}

void testBits() {
  using namespace BytePhys;
  u32 s = 12345u;
  auto rnd = [&s]() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  };
  for (u32 pos = 0; pos < STATE_BITS; ++pos) {
    for (u32 len = 1; len <= MAX_MEMBER_BITS && pos + len <= STATE_BITS;
         ++len) {
      P4Atom a = P4Atom::makeAtom(4u);
      a.mStg[0] = rnd();
      a.mStg[1] = rnd();
      a.mData0 = (u16) rnd();
      const P4Atom before = a;
      u32 want = 0u;
      for (u32 i = 0; i < len; ++i)
        want |= (u32) stateBit(a, pos + i) << i;
      check(getBits(a, pos, len) == want, "getBits");
      const u32 v = rnd() & lenMask(len);
      setBits(a, pos, len, v);
      bool ok = a.mParityAndType == before.mParityAndType;
      for (u32 i = 0; i < STATE_BITS; ++i) {
        const bool b = i >= pos && i < pos + len ? (v >> (i - pos)) & 1u
                                                 : stateBit(before, i);
        ok = ok && stateBit(a, i) == b;
      }
      check(ok, "setBits changed the wrong bits");
    }
  }

  // setm saturates to the declared width; add and sub saturate
  BytePhys::Machine vm{};
  auto run = [&vm](const std::vector<u8> & code, Env & e) {
    RawImage ri(code, &T_LAYOUT);
    vm.run(ri.prog, e, 1000u);
  };
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom before = e.mW[0];
    run({OP_PUSH8, 0, OP_PUSH8, 200, OP_SETM, 0, 0, OP_HALT1}, e);
    check(getBits(e.mW[0], 3u, 4u) == 15u, "setm saturates (200 -> 15)");
    P4Atom x = e.mW[0];
    setBits(x, 3u, 4u, getBits(before, 3u, 4u));
    check(memcmp(&x, &before, sizeof x) == 0, "setm touched other bits");
    run({OP_PUSH8, 0, OP_PUSH8, 0xab, OP_SETM, 1, 0, OP_PUSH8, 0,
         OP_GETM, 1, 0, OP_HALT1}, e);
    check(vm.mStack[0] == 0xabu, "straddling member round trip");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    run({OP_PUSH32, 0xff, 0xff, 0xff, 0xff, OP_PUSH8, 5, OP_ADD,
         OP_PUSH8, 3, OP_PUSH8, 5, OP_SUB, OP_HALT1}, e);
    check(vm.mStack[0] == 0xffffffffu, "add saturates");
    check(vm.mStack[1] == 0u, "sub saturates");
  }
  // the programs: age at its limit stays there (native would wrap to 0)
  {
    Env e(1u, 0);
    fillWindow(e, 1u, BP_TYPE_BFB4);
    e.mW[0].mStg[1] = 0xffffffffu;
    vm.run(PROG_BFB4, e, 10'000u);
    bool all = true;
    for (u32 i = 0; i < SITES; ++i) all = all && e.mW[i].mStg[1] == ~0u;
    check(all, "FB4 age saturates");
  }
  {
    Env e(1u, 1);                       // oneIn always true: the splat path
    fillWindow(e, 1u, BP_TYPE_BFB1);
    e.mW[0].mStg[0] = 0u;
    e.mW[0].mStg[1] = 0xfffffffeu;
    vm.run(PROG_BFB1, e, 10'000u);
    check(e.mW[0].mStg[1] == 0xffffffffu, "FB1 age saturates");
    check(e.mW[0].mStg[0] == 5u, "FB1 fuse set on the splat path");
  }
  // frames: sum(n) = n + sum(n - 1), recursively, reading its own n
  // again AFTER the recursive call returns
  {
    const u8 SUM = 1u, REC = 10u;       // function 1; offset in its chunk
    RawImage ri({{OP_PUSH8, 5, OP_CALL, SUM, 0, OP_HALT1},          // main
                 {OP_ENTER, 1, 1, OP_LOADL, 0, OP_JNZ, REC,         // sum(n)
                  OP_PUSH8, 0, OP_RET,                              // n == 0
                  OP_LOADL, 0, OP_PUSH8, 1, OP_SUB, OP_CALL, SUM, 0,  // rec
                  OP_LOADL, 0, OP_ADD, OP_RET}},
                {0u, 1u}, nullptr);
    check(ri.bad == 0u, "the sum image verifies");
    Env e(1u, 0);
    fillWindow(e, 1u);
    check(vm.run(ri.prog, e, 1000u), "sum halts with halt1");
    check(vm.mStack[0] == 15u, "recursive sum(5) == 15");
  }
  {
    const std::vector<u8> code = {
      OP_PUSH8, 3, OP_PUSH8, 4,
      OP_ENTER, 2, 3,           // slot 0 = 3, slot 1 = 4, slot 2 = 0
      OP_PUSH8, 7, OP_STOREL, 2,
      OP_LOADL, 2, OP_LOADL, 1, OP_LOADL, 0, OP_HALT1,
    };
    Env e(1u, 0);
    fillWindow(e, 1u);
    RawImage ri(code, nullptr);
    vm.run(ri.prog, e, 1000u);
    check(vm.mStack[0] == 7u && vm.mStack[1] == 4u && vm.mStack[2] == 3u,
          "enter moves args in order; storel/loadl");
  }
  printf("frames: recursion with locals, args, storel/loadl\n");

  // the generated type -> program table the EWP dispatches through
  check(entryFor(g_phys, BP_TYPE_BFB1) == BP_FUNC_BFB1, "entryFor BFB1");
  check(entryFor(g_phys, BP_TYPE_BFB4) == BP_FUNC_BFB4, "entryFor BFB4");
  check(entryFor(g_phys, BP_FIRST_TYPE - 1u) == NO_FUNC,
        "entryFor native");
  check(entryFor(g_phys, BP_FIRST_TYPE + BP_TYPE_COUNT) == NO_FUNC,
        "entryFor past the table");
  check(entryFor(g_phys, 0xffffu) == NO_FUNC, "entryFor huge");
  check(entryFor(g_phys, 0u) == NO_FUNC, "entryFor 0");
  printf("members: getBits/setBits match a bit model at every pos/len; "
         "setm, add, sub, FB1/FB4 ages saturate\n");
}

}  // namespace

/* Run a test-only ulam-lite program with its element at the center;
   return the center atom afterwards. */
P4Atom runTest(const BytePhys::Program & p, u16 type, Env & e) {
  BytePhys::Machine vm{};
  e.mW[0] = P4Atom::makeAtom(type);
  try {
    check(vm.run(p, e, 10'000u), "test program commits");
  } catch (const Fault & f) {
    printf("FAIL: test program faulted %u\n", f.code);
    ++g_bad;
  }
  return e.mW[0];
}

void testCompiler() {
  using namespace BytePhys;
  using namespace BytePhys::Tests;
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(testProg(BP_FUNC_TMETHODS), BP_TYPE_TMETHODS, e);
    check(getBits(a, 0u, 16u) == 15u, "methods: recursive sum(5)");
    check(getBits(a, 16u, 16u) == 18u, "methods: max with params");
    check(getBits(a, 32u, 16u) == 15u, "methods: Unsigned(4) param clamps");
    check(getBits(a, 48u, 16u) == 15u, "methods: Unsigned(4) return clamps");
    check(getBits(a, 64u, 16u) == 2u, "methods: Bool returns");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(testProg(BP_FUNC_TLOOPS), BP_TYPE_TLOOPS, e);
    check(getBits(a, 0u, 16u) == 5u, "loops: for with || && !");
    check(getBits(a, 16u, 16u) == 7u, "loops: while(true) + break");
    check(getBits(a, 32u, 16u) == 25u, "loops: continue");
    check(getBits(a, 48u, 16u) == 3u, "loops: comparisons as values");
    check(getBits(a, 64u, 16u) == 8u, "loops: break leaves the inner loop");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(testProg(BP_FUNC_TLOCALS), BP_TYPE_TLOCALS, e);
    check(getBits(a, 0u, 16u) == 18u, "locals: Unsigned(4) saturates");
    check(getBits(a, 16u, 16u) == 3u, "locals: shadowing");
    check(getBits(a, 32u, 16u) == 16u,
          "locals: re-zeroed in a loop; Bool local");
    check(getBits(a, 48u, 32u) == 0xffffffffu, "locals: 32-bit saturation");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    e.mW[1] = P4Atom::makeEmptyAtom();
    e.mW[2] = P4Atom::makeAtom(BP_TYPE_TWINDOW);
    e.mW[3] = P4Atom::makeAtom(BP_TYPE_TLOOPS);
    e.mW[3].mStg[1] = 0x12345678u;
    e.mW[4] = P4Atom::makeAtom(4u);
    e.mW[6] = P4Atom::makeAtom(4u);
    const P4Atom three = e.mW[3];
    const P4Atom a = runTest(testProg(BP_FUNC_TWINDOW), BP_TYPE_TWINDOW, e);
    check(getBits(a, 0u, 4u) == 15u, "window: Unsigned(4) member saturates");
    check(getBits(a, 4u, 1u) == 1u, "window: Bool member stores 1");
    check(getBits(a, 5u, 8u) == 31u, "window: is tests, short circuits");
    check(e.mDraws.size() == 1u, "window: exactly one random draw");
    check(memcmp(&e.mW[5], &three, sizeof three) == 0, "window: ew[5] = ew[3]");
    check(e.mW[6].isEmpty(), "window: ew[6] = Empty");
    check(memcmp(&e.mW[7], &e.mW[0], sizeof a) == 0, "window: ew[7] = self");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(testProg(BP_FUNC_TFALLBACK), BP_TYPE_TFALLBACK, e);
    check(getBits(a, 0u, 16u) == 6u, "fallback: body assigns the counter");
    check(getBits(a, 16u, 16u) == 25u, "fallback: counter read too deep");
    check(getBits(a, 32u, 16u) == 12u, "fallback: outer counter, inner loop");
    check(getBits(a, 48u, 16u) == 107u,
          "fallback: return from stack-counter loops");
  }
  printf("compiler: test programs (methods, loops, locals, window, "
         "fallback) give "
         "their expected results\n");
}

/* The microbenchmark programs, as the EWP's runBench runs them: Bench
   atom at the center, BP_FUEL 10,000. */
void testBench() {
  using BytePhys::Machine;
  namespace BB = BytePhys::Bench;
  const u32 per = 50u * 8u;             // benchgen PASSES * COPIES
  u32 base = 0u;
  printf("bench insns:");
  for (u32 i = 0; i < BB::BP_PROG_COUNT; ++i) {
    Env e(7u + i, 0);
    fillWindow(e, i);
    e.mW[0] = P4Atom::makeAtom(BB::BP_TYPE_BENCH);
    e.mW[0].mData0 = (u16) (1234u + i);
    const Env before = e;
    Machine vm{};
    try {
      check(vm.run(benchProg(BB::BP_PROG_FUNCS[i]), e, 10'000u), "bench commits");
    } catch (const Fault & f) {
      printf("\nFAIL: bench %s faulted %u\n", BB::BP_PROG_NAMES[i], f.code);
      ++g_bad;
      continue;
    }
    printf(" %s %u", BB::BP_PROG_NAMES[i] + 4, vm.mExecuted);
    check(memcmp(&e.mW[1], &before.mW[1],
                 sizeof(P4Atom) * (BytePhys::SITES - 1u)) == 0,
          "bench leaves the neighbors alone");
    check(e.mW[0].getType() == BB::BP_TYPE_BENCH &&
          e.mW[0].mData0 == before.mW[0].mData0,
          "bench keeps the center's type and idx");
    if (i == 0u) base = vm.mExecuted;
    else check(vm.mExecuted > base && (vm.mExecuted - base) % per == 0u,
               "bench runs base + whole pattern copies");
  }
  printf("\n");
}

/* ByteCache.h: code run through the chunk cache behaves exactly as in
   place, for the physics (FB1 calls kabam: a second chunk) and for a
   function split across chunks (the test physics); the slot hint, scans
   (two chunks sharing a hint), FIFO eviction, rebinding to another
   image, and refreshOne (which also repairs a corrupted copy) behave
   and count right.  And the dispatch mask: an opcode byte
   past OP_COUNT faults BF_OPCODE (unverified code, as only corruption
   could make it). */
void testCodeCache() {
  using BytePhys::Program;
  using BytePhys::BCPView;
  using BytePhys::CodeCache;
  using BytePhys::BP_TYPE_BFB1;
  using BytePhys::BP_TYPE_BFB4;
  using BytePhys::OP_COUNT;
  using BytePhys::OP_HALT1;
  using BytePhys::BF_OPCODE;
  BytePhys::Machine vm{};
  CodeCache<12> cache{};
  cache.reset();
  const Program * progs[2] = {&PROG_BFB1, &PROG_BFB4};
  for (u32 seed = 1; seed <= 600; ++seed) {
    for (int mode = 0; mode < 3; ++mode) {
      const int fb = (int) (seed % 2u);
      const u16 ct = fb == 0 ? BP_TYPE_BFB1 : BP_TYPE_BFB4;
      Env a(seed, mode);
      Env b(seed, mode);
      fillWindow(a, seed, ct);
      fillWindow(b, seed, ct);
      bool ra = false;
      bool rb = false;
      try {
        ra = vm.run(*progs[fb], a, 10'000u);
        rb = vm.run(*progs[fb], b, 10'000u, cache);
      } catch (const Fault &) {
        check(false, "cache: a program faulted");
        continue;
      }
      check(ra == rb && sameWindow(a, b) && a.mDraws == b.mDraws,
            "cache: cached run differs from in place");
    }
  }
  const BCPView & img = g_phys;
  check(cache.mMisses <= img.mChunkCount && cache.mMisses >= 2u,
        "cache: each chunk misses once, then hits");
  // the test physics has a function longer than a chunk: run every
  // test program through a 2-slot cache (evictions mid-call) and in place
  {
    namespace T = BytePhys::Tests;
    CodeCache<2> two{};
    two.reset();
    u32 split = 0u;
    for (u32 f = 0; f < g_tests.mFuncCount; ++f) {
      const u32 c = g_tests.mFuncChunk[f];
      if (c + 1u < g_tests.mChunkCount &&
          (f + 1u == g_tests.mFuncCount ||
           g_tests.mFuncChunk[f + 1u] > c + 1u)) ++split;
    }
    check(split > 0u, "cache: the test image has a split function");
    for (u32 i = 0; i < T::BP_PROG_COUNT; ++i) {
      for (u32 seed = 1; seed <= 20; ++seed) {
        Env a(seed, 0);
        Env b(seed, 0);
        fillWindow(a, seed, 4u);
        fillWindow(b, seed, 4u);
        u32 fa = 0u;
        u32 fbb = 0u;
        bool ra = false;
        bool rb = false;
        try { ra = vm.run(testProg(T::BP_PROG_FUNCS[i]), a, 10'000u); }
        catch (const Fault & f) { fa = f.code; }
        try { rb = vm.run(testProg(T::BP_PROG_FUNCS[i]), b, 10'000u, two); }
        catch (const Fault & f) { fbb = f.code; }
        check(fa == fbb && ra == rb && sameWindow(a, b),
              "cache: a 2-slot cache changes a test program's run");
      }
    }
    check(two.mMisses > 2u, "cache: the 2-slot cache evicted");
  }
  // hint, scan and FIFO: chunks through two slots (BP_IMAGE has 3)
  check(img.mChunkCount >= 3u, "cache: the physics has 3 chunks");
  CodeCache<2> ff{};
  ff.reset();
  ff.bind(img);
  const u8 * p0 = ff.chunk(0);
  const u8 * p1 = ff.chunk(1);
  check(ff.mMisses == 2u && ff.mScans == 0u && p0 != p1,
        "fifo: two misses fill two slots");
  check(memcmp(p1, img.mCode + img.mChunkStart[1],
               BytePhys::chunkLength(img, 1)) == 0,
        "fifo: the slot holds the chunk's bytes");
  check(ff.chunk(0) == p0 && ff.chunk(1) == p1 && ff.mMisses == 2u &&
        ff.mScans == 0u, "hint: hits by hint, nothing counted");
  ff.chunk(2);                          // evicts chunk 0 (first in)
  check(ff.mMisses == 3u && ff.chunk(1) == p1 && ff.mMisses == 3u,
        "fifo: the second one stayed");
  ff.chunk(0);
  check(ff.mMisses == 4u, "fifo: the first one in was evicted");
  ff.bind(img);
  check(ff.mMisses == 4u && ff.chunk(0) != nullptr && ff.mMisses == 4u,
        "bind: the same image keeps the slots");
  // two chunks sharing a hint entry: a 1-entry hint table
  CodeCache<2, BytePhys::CHUNK_MAX, 1> sh{};
  sh.reset();
  sh.bind(img);
  const u8 * s0 = sh.chunk(0);
  const u8 * s1 = sh.chunk(1);          // overwrites the hint
  check(sh.chunk(0) == s0 && sh.mScans == 1u && sh.mMisses == 2u,
        "hint: a shared entry falls back to a scan hit");
  check(sh.chunk(0) == s0 && sh.mScans == 1u,
        "hint: the scan rewrote the hint");
  check(sh.chunk(1) == s1 && sh.mScans == 2u, "hint: and back");
  // rebinding to another image empties the cache
  {
    namespace T = BytePhys::Tests;
    ff.bind(g_tests);
    const u32 m = ff.mMisses;
    const u8 * t0 = ff.chunk(0);
    check(ff.mMisses == m + 1u &&
          memcmp(t0, g_tests.mCode, g_tests.mChunkStart[1]) == 0,
          "bind: another image empties the cache");
  }
  // refreshOne: round robin, repairs a corrupted copy, skips empties
  CodeCache<2> rf{};
  rf.reset();
  rf.refreshOne();                      // slot 0 (the turn advances)
  check(rf.mRefreshes == 0u, "refresh: nothing bound, nothing done");
  rf.bind(img);
  u8 * q = const_cast<u8 *>(rf.chunk(2));
  q[0] ^= 0x40u;                        // a flipped bit
  rf.refreshOne();                      // slot 1: empty, skipped
  rf.refreshOne();                      // slot 0: chunk 2
  check(rf.mRefreshes == 1u &&
        memcmp(q, img.mCode + img.mChunkStart[2],
               BytePhys::chunkLength(img, 2)) == 0,
        "refresh: restores a corrupted copy, skips an empty slot");
  rf.refreshOne();                      // slot 1 again: still empty
  rf.refreshOne();                      // slot 0 again
  check(rf.mRefreshes == 2u, "refresh: round robin back to slot 0");
  // the dispatch mask: bytes at or past OP_COUNT cannot jump wild
  auto runRaw = [&](std::vector<u8> code) -> u32 {
    Env e(1u, 0);
    fillWindow(e, 1u, 4u);
    RawImage ri(code, nullptr);       // fails verifyBCP: run it anyway,
    BytePhys::viewBCP((const u8 *) ri.blob.data(), ri.img);  // as only
    try {                               // corruption could
      vm.run(ri.prog, e, 100u);
    } catch (const Fault & f) {
      return f.code;
    }
    return 0u;
  };
  // since OP_NEW OP_COUNT is 32: every masked byte is an opcode, and a
  // byte above the mask runs the opcode it masks to
  static_assert((u32) OP_COUNT == (u32) BytePhys::Machine::OPS_SLOTS,
                "update the mask checks: BF_OPCODE slots are back");
  check(runRaw({(u8) (0x20u | OP_HALT1)}) == 0u,
        "mask: a flip above the mask runs the masked opcode");
  check(runRaw({(u8) (0xe0u | BytePhys::OP_HALT0)}) == 0u,
        "mask: 0xe0 runs halt0");
  printf("code cache: %u scans, %u misses, %u bytes copied; "
         "cache<12> %zu bytes\n",
         (unsigned) cache.mScans, (unsigned) cache.mMisses,
         (unsigned) cache.mBytesCopied, sizeof(cache));
}

/* BCP blobs (BytePhys.h 'BCP'): bpasm.py and packBCP pack the same
   bytes; verifyBCP / openBCP refuse every kind of damage to the blob
   itself, and a flipped code byte under a fixed checksum still fails. */
void testBCP() {
  using namespace BytePhys;
  // repack the physics from its opened view: identical to bpasm's blob
  {
    std::vector<std::vector<u8>> chunks;
    for (u32 c = 0; c < g_phys.mChunkCount; ++c)
      chunks.emplace_back(g_phys.mCode + g_phys.mChunkStart[c],
                          g_phys.mCode + g_phys.mChunkStart[c + 1u]);
    std::vector<u16> fch(g_phys.mFuncChunk,
                         g_phys.mFuncChunk + g_phys.mFuncCount);
    std::vector<u16> tf(g_phys.mTypeFunc,
                        g_phys.mTypeFunc + g_phys.mTypeCount);
    const std::vector<u32> w = packBCP(chunks, fch, &g_phys.mLayout,
                                       g_phys.mFirstType, tf);
    check(w.size() * 4u == sizeof(BP_BCP) &&
          memcmp(w.data(), BP_BCP, sizeof(BP_BCP)) == 0,
          "bcp: packBCP == bpasm.py's blob");
  }
  check(g_phys.mTypeCount == BP_TYPE_COUNT &&
        g_phys.mFirstType == BP_FIRST_TYPE, "bcp: the type table");
  // damage, one kind at a time, on a copy of the physics blob
  const u32 n = sizeof(BP_BCP);
  auto damaged = [&](u32 at, u8 x, bool fixSum) -> u32 {
    std::vector<u32> w(n / 4u);
    memcpy(w.data(), BP_BCP, n);
    u8 * b = (u8 *) w.data();
    b[at] ^= x;
    if (fixSum) {
      const u32 h = bcpChecksum(b, n);
      memcpy(b + BCPH_CHECKSUM, &h, 4u);  // (little-endian host)
    }
    BCPView v{};
    return openBCP(b, n, v);
  };
  check(damaged(BCPH_MAGIC, 1u, true) == BCP_BAD_MAGIC, "bcp: magic");
  check(damaged(BCPH_VERSION, 1u, true) == BCP_BAD_VERSION, "bcp: version");
  check(damaged(BCPH_OPCOUNT, 1u, true) == BCP_BAD_VERSION,
        "bcp: opcode count");
  check(damaged(BCPH_SIZE, 4u, true) == BCP_BAD_SIZE, "bcp: size field");
  check(damaged(BCP_HEADER_BYTES + 5u, 1u, false) == BCP_BAD_CHECKSUM,
        "bcp: a flipped code byte breaks the checksum");
  check(damaged(BCPH_OFF_FUNC_CHUNK, 0x02u, true) == BCP_BAD_TABLE,
        "bcp: a misaligned table");
  check(damaged(BCPH_OFF_ATS + 1u, 0x10u, true) == BCP_BAD_TABLE,
        "bcp: a table past the end");
  check(damaged(BCPH_RESERVED, 1u, true) == BCP_BAD_TABLE,
        "bcp: reserved words");
  {
    const u32 oats = bcpRd32(BP_BCP, BCPH_OFF_ATS);
    check(damaged(oats + 3u, 0x40u, true) == BCP_BAD_LAYOUT,
          "bcp: a member over 32 bits");
    const u32 orefs = bcpRd32(BP_BCP, BCPH_OFF_REFS);
    check(damaged(orefs + 2u, 0x08u, true) == BCP_BAD_LAYOUT,
          "bcp: a member ref past the ats");
    const u32 ot = bcpRd32(BP_BCP, BCPH_OFF_TYPE_FUNC);
    check(damaged(ot, 0x40u, true) == BCP_BAD_TYPES,
          "bcp: a type naming no function");
  }
  {
    // the first opcode byte made unknown, checksum fixed: the code check
    const u32 oc = bcpRd32(BP_BCP, BCPH_OFF_CODE);
    check(damaged(oc, 0xe0u, true) == BF_OPCODE,
          "bcp: verify still checks the code");
  }
  {
    BCPView v{};
    check(openBCP(BP_BCP, n - 4u, v) == BCP_BAD_SIZE &&
          v.mBase == nullptr, "bcp: room smaller than the blob");
    std::vector<u32> w(n / 4u + 1u);
    memcpy((u8 *) w.data() + 1, BP_BCP, n);
    check(openBCP((const u8 *) w.data() + 1, n, v) == BCP_BAD_SIZE,
          "bcp: a misaligned blob");
    check(verifyBCP(BP_BCP, n + 4u) == BCP_BAD_SIZE &&
          verifyBCP(BP_BCP, 60u) == BCP_BAD_SIZE, "bcp: sizes");
  }
  printf("bcp: physics blob %u bytes, packers agree, damage refused\n",
         (unsigned) n);
}

/* Native DReg and Res, copied from ewp Physics.cpp (PHY_DREG,
   PHY_RES) with the bytecode type numbers, for testDReg. */
bool nativeDReg(Env & ew, u16 dreg, u16 res) {
  const u32 ngbsn = ew.between(1u, 4u);
  P4Atom & na = ew.atom(ngbsn);
  const u32 natype = na.getType();
  P4Atom & ca = ew.atom(0);
  if (natype == P4Atom::INACCESSIBLE_TYPE) {
    // DO NOTHING! DON'T GO THERE!
  } else if (natype == P4Atom::EMPTY_TYPE) {
    if (ew.oneIn(250u)) na = ca;
    else if (ew.oneIn(25u)) na = P4Atom::makeAtom(res);
    else std::swap(ca, na);
  } else if (natype == dreg) {
    if (ew.oneIn(20u)) na = P4Atom::makeEmptyAtom();
  } else if (ew.oneIn(40u)) {
    na = P4Atom::makeEmptyAtom();
    std::swap(ca, na);
  }
  return true;
}

bool nativeRes(Env & ew) {
  const u32 ngbsn = ew.between(1u, 4u);
  P4Atom & na = ew.atom(ngbsn);
  if (na.getType() == P4Atom::EMPTY_TYPE) std::swap(ew.atom(0), na);
  return true;
}

/* DReg and Res in ulam-lite (scripts/ulamlite/dreg, physics/dreg.bcp)
   do exactly what the native ones do: same windows, same draws in the
   same order, over windows mixing empty, DReg, Res, another type and
   inaccessible sites.  And OP_NEW: verify refuses INACCESSIBLE, run
   checks the site. */
void testDReg() {
  namespace D = BytePhys::DReg;
  BytePhys::Machine vm{};
  const BytePhys::Program progs[2] = {{&g_dreg, D::BP_FUNC_DREG},
                                      {&g_dreg, D::BP_FUNC_RES}};
  const u16 kinds[6] = {0u, 0u, D::BP_TYPE_DREG, D::BP_TYPE_RES, 4u,
                        P4Atom::INACCESSIBLE_TYPE};
  long runs = 0;
  u32 news = 0;
  for (int mode = 0; mode < 3; ++mode) {
    for (u32 seed = 1; seed <= 4000; ++seed) {
      for (int k = 0; k < 2; ++k) {
        Env n(seed * 104729u + (u32) mode, mode);
        Env b(seed * 104729u + (u32) mode, mode);
        u32 s = seed * 2654435761u + 7u;
        for (u32 i = 0; i < BytePhys::SITES; ++i) {
          s ^= s << 13;
          s ^= s >> 17;
          s ^= s << 5;
          n.mW[i] = P4Atom::makeAtom(kinds[s % 6u]);
          // state on the others; an empty atom is the clean one (a
          // native swap moves the empty atom, bytecode makes a new one)
          if (kinds[s % 6u] != 0u) n.mW[i].mStg[0] = s;
        }
        n.mW[0] = P4Atom::makeAtom(k == 0 ? D::BP_TYPE_DREG
                                          : D::BP_TYPE_RES);
        n.mW[0].mStg[1] = seed;
        memcpy(b.mW, n.mW, sizeof(n.mW));
        u32 resBefore = 0;
        for (u32 i = 1; i <= 4u; ++i)
          resBefore += n.mW[i].getType() == D::BP_TYPE_RES;
        const bool rn = k == 0 ? nativeDReg(n, D::BP_TYPE_DREG,
                                            D::BP_TYPE_RES)
                               : nativeRes(n);
        bool rb = false;
        try {
          rb = vm.run(progs[k], b, 10'000u);
        } catch (const Fault &) {
          check(false, "bytecode DReg/Res faulted");
          continue;
        }
        check(rn == rb, "DReg/Res: results differ");
        check(sameWindow(n, b), k == 0 ? "DReg windows differ"
                                       : "Res windows differ");
        check(n.mDraws == b.mDraws, "DReg/Res: draws differ");
        u32 resAfter = 0;
        for (u32 i = 1; i <= 4u; ++i)
          resAfter += b.mW[i].getType() == D::BP_TYPE_RES;
        news += k == 0 && resAfter > resBefore;
        ++runs;
      }
    }
  }
  check(news > 0u, "DReg: never made a Res (OP_NEW untested)");
  // OP_NEW: the type is checked once, by verify; the site at run time
  using namespace BytePhys;
  const u8 P8 = OP_PUSH8;
  check(faultOf({P8, 41, OP_NEW, 3, 0, OP_HALT1}) == BF_SITE,
        "new: site 41");
  {
    RawImage ok({P8, 1, OP_NEW, 0xfe, 0x01, OP_HALT1}, nullptr);
    RawImage bad({P8, 1, OP_NEW, 0xff, 0x01, OP_HALT1}, nullptr);
    check(ok.bad == 0u, "new: type 510 verifies");
    check(bad.bad == BF_TYPE, "new: INACCESSIBLE (511) refused");
  }
  printf("dreg: %ld DReg/Res events match native, %u made a Res\n",
         runs, (unsigned) news);
}

int main() {
  testBCP();
  testEquivalence();
  testDReg();
  testFaults();
  testBits();
  testCompiler();
  testBench();
  testCodeCache();
  if (g_bad) {
    printf("bytephys_test: %d FAILED\n", g_bad);
    return 1;
  }
  printf("bytephys_test: all checks passed\n");
  return 0;
}
