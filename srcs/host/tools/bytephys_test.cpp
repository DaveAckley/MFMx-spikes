// bytephys_test.cpp -- native test of the bytecode physics spike:
// srcs/shared/include/BytePhys.h (the machine), BytePrograms.h (fb1.bp,
// fb4.bp assembled by scripts/bytephys/bpasm.py) against NativeFB.h (the
// EWP's native FB1/FB4, the very same code Physics.cpp runs).
// Build/run: see the bytephys_test line of run_native_tests.sh.
//
// Checks:
//   * bytecode FB1/FB4 leave EXACTLY the window native FB1/FB4 leave, for
//     many random windows and counter states, under several random
//     streams (uniform, always-low, always-high), drawing the same
//     numbers in the same order
//   * every machine fault is caught: stack under/overflow, call
//     under/overflow (runaway recursion), bad opcode, jump past the end,
//     running off the end, bad site, bad/read-only field, fuel
// and prints the bytecode instructions per event (mean, max).
#include <cstdio>
#include <cstring>
#include <vector>

#include "BytePrograms.h"
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

void fillWindow(Env & e, u32 seed) {
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
  const u32 stg1s[] = {0u, 19u, 20u, 21u, 0xfffffffeu, 0xffffffffu};
  e.mW[0] = P4Atom::makeAtom(4u);
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
        fillWindow(n, seed);
        fillWindow(b, seed);
        const bool rn = fb == 0 ? nativeFB1(n) : nativeFB4(n);
        bool rb = false;
        try {
          rb = vm.run(fb == 0 ? BytePhys::BP_PROG_FB1
                              : BytePhys::BP_PROG_FB4, b, 10'000u);
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
         (unsigned) BytePhys::BP_PROG_FB1.mLength,
         (unsigned) BytePhys::BP_PROG_FB4.mLength,
         sizeof(BytePhys::Machine));
}

u32 faultOf(const std::vector<u8> & code, u32 fuel = 10'000u) {
  BytePhys::Machine vm;
  BytePhys::Program p{code.data(), (u16) code.size()};
  Env e(1u, 0);
  fillWindow(e, 1u);
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
  check(faultOf({OP_DROP}) == BF_STACK_UNDER, "stack underflow");
  std::vector<u8> over;
  for (u32 i = 0; i <= Machine::STACK_SIZE; ++i) {
    over.push_back(P8);
    over.push_back(1);
  }
  over.push_back(OP_HALT1);
  check(faultOf(over) == BF_STACK_OVER, "stack overflow");
  check(faultOf({OP_RET}) == BF_CALL_UNDER, "return with no call");
  check(faultOf({OP_CALL, 0, 0}) == BF_CALL_OVER, "runaway recursion");
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
  printf("faults: every machine fault caught\n");
}

}  // namespace

int main() {
  testEquivalence();
  testFaults();
  if (g_bad) {
    printf("bytephys_test: %d FAILED\n", g_bad);
    return 1;
  }
  printf("bytephys_test: all checks passed\n");
  return 0;
}
