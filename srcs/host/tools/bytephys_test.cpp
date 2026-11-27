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
#include <vector>

#include "BytePrograms.h"
#include "BytePhysTests.h"   // test-only physics, scripts/ulamlite/tests
#include "ByteBench.h"       // microbenchmark, scripts/bytephys/benchgen.py
#include "NativeFB.h"

using namespace MFM;

namespace {

int g_bad = 0;

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
  BytePhys::Machine vm;
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
          rb = vm.run(fb == 0 ? BytePhys::BP_PROG_BFB1
                              : BytePhys::BP_PROG_BFB4, b, 10'000u);
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
  printf("code size: FB1 %u bytes, FB4 %u bytes; machine %zu bytes\n",
         (unsigned) BytePhys::BP_PROG_BFB1.mLength,
         (unsigned) BytePhys::BP_PROG_BFB4.mLength,
         sizeof(BytePhys::Machine));
}

/* A hand-made layout for the member tests: member 0 is 4 bits at state
   bit 3 of type 4, member 1 is 8 bits at 28 (straddles mStg[0]/mStg[1])
   of type 4 and 3 bits at 77 (top of mData0) of type 9. */
const BytePhys::MemberRef T_REFS[2] = {{0u, 1u}, {1u, 2u}};
const BytePhys::MemberAt T_ATS[3] = {{4u, 3u, 4u}, {4u, 28u, 8u},
                                     {9u, 77u, 3u}};
const BytePhys::Layout T_LAYOUT = {T_REFS, 2u, T_ATS};

u32 faultOf(const std::vector<u8> & code, u32 fuel = 10'000u,
            const BytePhys::Layout * lay = &T_LAYOUT, u16 centerType = 4u) {
  BytePhys::Machine vm;
  BytePhys::Program p{code.data(), (u16) code.size(), lay};
  // what verify() rejects never runs (the EWP's programs are verified at
  // compile time): report it as the fault it stands for
  if (const u32 v = BytePhys::verify(p)) return v;
  Env e(1u, 0);
  fillWindow(e, 1u, centerType);
  try {
    vm.run(p, e, fuel);
  } catch (const Fault & f) {
    return f.code;
  }
  return 0u;
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
  check(faultOf({OP_JMP, 4, 0, OP_PUSH8, 1, OP_HALT1}) == BF_PC,
        "jump into an immediate");
  check(faultOf({OP_JZ, 0, 0}) == BF_PC, "jz as the last instruction");
  check(faultOf({}) == BF_PC, "empty program");
  check(faultOf({OP_HALT0, 0xee}) == BF_OPCODE,
        "bad opcode where control never goes");
  check(faultOf({0xee}) == BF_OPCODE, "bad opcode");
  check(faultOf({OP_JMP, 9, 0}) == BF_PC, "jump past the end");
  check(faultOf({P8, 1}) == BF_PC, "running off the end");
  check(faultOf({OP_PUSH32, 1}) == BF_PC, "truncated immediate");
  check(faultOf({P8, 41, OP_EMPTY, OP_HALT1}) == BF_SITE, "site 41");
  check(faultOf({P8, 0, P8, 5, OP_SETF, F_TYPE, OP_HALT1}) == BF_FIELD,
        "type is read-only");
  check(faultOf({P8, 0, OP_GETF, 9, OP_HALT1}) == BF_FIELD, "bad field");
  check(faultOf({OP_JMP, 0, 0}, 1000u) == BF_FUEL, "infinite loop");
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
  check(faultOf({OP_PUSH8, 0, OP_JZ, 0, 0, OP_HALT1}, 50u) == BF_FUEL,
        "a jz loop runs out of fuel");
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
  BytePhys::Machine vm;
  auto run = [&vm](const std::vector<u8> & code, Env & e) {
    BytePhys::Program p{code.data(), (u16) code.size(), &T_LAYOUT};
    vm.run(p, e, 1000u);
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
    vm.run(BP_PROG_BFB4, e, 10'000u);
    bool all = true;
    for (u32 i = 0; i < SITES; ++i) all = all && e.mW[i].mStg[1] == ~0u;
    check(all, "FB4 age saturates");
  }
  {
    Env e(1u, 1);                       // oneIn always true: the splat path
    fillWindow(e, 1u, BP_TYPE_BFB1);
    e.mW[0].mStg[0] = 0u;
    e.mW[0].mStg[1] = 0xfffffffeu;
    vm.run(BP_PROG_BFB1, e, 10'000u);
    check(e.mW[0].mStg[1] == 0xffffffffu, "FB1 age saturates");
    check(e.mW[0].mStg[0] == 5u, "FB1 fuse set on the splat path");
  }
  // frames: sum(n) = n + sum(n - 1), recursively, reading its own n
  // again AFTER the recursive call returns
  {
    const u8 SUM = 6u, REC = 17u;
    const std::vector<u8> code = {
      OP_PUSH8, 5, OP_CALL, SUM, 0, OP_HALT1,           //  0: main
      OP_ENTER, 1, 1, OP_LOADL, 0, OP_JNZ, REC, 0,      //  6: sum(n)
      OP_PUSH8, 0, OP_RET,                              // 14: n == 0
      OP_LOADL, 0, OP_PUSH8, 1, OP_SUB, OP_CALL, SUM, 0,  // 17: rec
      OP_LOADL, 0, OP_ADD, OP_RET,                      // 25
    };
    Env e(1u, 0);
    fillWindow(e, 1u);
    BytePhys::Program p{code.data(), (u16) code.size(), nullptr};
    check(vm.run(p, e, 1000u), "sum halts with halt1");
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
    BytePhys::Program p{code.data(), (u16) code.size(), nullptr};
    vm.run(p, e, 1000u);
    check(vm.mStack[0] == 7u && vm.mStack[1] == 4u && vm.mStack[2] == 3u,
          "enter moves args in order; storel/loadl");
  }
  printf("frames: recursion with locals, args, storel/loadl\n");

  // the generated type -> program table the EWP dispatches through
  check(programFor(BP_TYPE_BFB1) == &BP_PROG_BFB1, "programFor BFB1");
  check(programFor(BP_TYPE_BFB4) == &BP_PROG_BFB4, "programFor BFB4");
  check(programFor(BP_FIRST_TYPE - 1u) == nullptr, "programFor native");
  check(programFor(BP_FIRST_TYPE + BP_TYPE_COUNT) == nullptr,
        "programFor past the table");
  check(programFor(0xffffu) == nullptr, "programFor huge");
  printf("members: getBits/setBits match a bit model at every pos/len; "
         "setm, add, sub, FB1/FB4 ages saturate\n");
}

}  // namespace

/* Run a test-only ulam-lite program with its element at the center;
   return the center atom afterwards. */
P4Atom runTest(const BytePhys::Program & p, u16 type, Env & e) {
  BytePhys::Machine vm;
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
    const P4Atom a = runTest(BP_PROG_TMETHODS, BP_TYPE_TMETHODS, e);
    check(getBits(a, 0u, 16u) == 15u, "methods: recursive sum(5)");
    check(getBits(a, 16u, 16u) == 18u, "methods: max with params");
    check(getBits(a, 32u, 16u) == 15u, "methods: Unsigned(4) param clamps");
    check(getBits(a, 48u, 16u) == 15u, "methods: Unsigned(4) return clamps");
    check(getBits(a, 64u, 16u) == 2u, "methods: Bool returns");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(BP_PROG_TLOOPS, BP_TYPE_TLOOPS, e);
    check(getBits(a, 0u, 16u) == 5u, "loops: for with || && !");
    check(getBits(a, 16u, 16u) == 7u, "loops: while(true) + break");
    check(getBits(a, 32u, 16u) == 25u, "loops: continue");
    check(getBits(a, 48u, 16u) == 3u, "loops: comparisons as values");
    check(getBits(a, 64u, 16u) == 8u, "loops: break leaves the inner loop");
  }
  {
    Env e(1u, 0);
    fillWindow(e, 1u);
    const P4Atom a = runTest(BP_PROG_TLOCALS, BP_TYPE_TLOCALS, e);
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
    const P4Atom a = runTest(BP_PROG_TWINDOW, BP_TYPE_TWINDOW, e);
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
    const P4Atom a = runTest(BP_PROG_TFALLBACK, BP_TYPE_TFALLBACK, e);
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
    Machine vm;
    try {
      check(vm.run(*BB::BP_PROG_LIST[i], e, 10'000u), "bench commits");
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

int main() {
  testEquivalence();
  testFaults();
  testBits();
  testCompiler();
  testBench();
  if (g_bad) {
    printf("bytephys_test: %d FAILED\n", g_bad);
    return 1;
  }
  printf("bytephys_test: all checks passed\n");
  return 0;
}
