// physguard_test.cpp -- native test of RecoverPoint (cross/include/
// RecoverPoint.h) with libc setjmp: the install / jump / clear protocol that
// Physics::guardedPhysics and PrepareToDie rely on (recoverNow on a per-hart
// slot, RecoverSuspend), plus the recovery rule (a jump commits the OLD
// window with the center erased).  The RISC-V setjmp itself is tested on
// hardware (t6sjlj, ERISC_NOTES 9.22).  Build/run:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -I../../cross/include -I../../shared/include physguard_test.cpp -o /tmp/pgt && /tmp/pgt
#define RECOVER_HOSTTEST 1
#include <cstdio>
#include <cstring>
#include "RecoverPoint.h"

using namespace MFM;

namespace {
int g_bad = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    ++g_bad;
  }
}

struct Win {
  u32 a[8];
};

// Stand-in for fAll.mRecover: one slot per hart.  Two harts here.
RecoverPoint * g_slot[2];

// A stand-in for Physics on hart `h`: same shape, same recovery rule.
struct Mini {
  u32 mHart;
  RecoverPoint mRecover;
  Win fast;
  enum R { GR_FALSE, GR_TRUE, GR_JUMPED };

  // what FAIL does on this hart
  void fail(s32 code, u32 file, u32 line) {
    recoverNow(g_slot[mHart], code, file, line);   // jumps if installed
    mFellThrough = true;                           // no recovery: PrepareToDie would hang
  }
  bool mFellThrough;

  __attribute__((noinline)) bool physics(int mode) {
    if (mode == 1) {             // scribble then fail, from one frame down
      for (u32 i = 1; i < 5; ++i) fast.a[i] = 99;
      fail(-77, 12, 345);
    }
    if (mode == 2) {             // physics declines
      fast.a[1] = 55;            // ...after touching the window
      return false;
    }
    if (mode == 3) {             // engine service wraps itself: FAIL inside it hangs
      RecoverSuspend s(g_slot[mHart]);
      fail(-66, 1, 2);
      return true;
    }
    fast.a[1] = 7;
    return true;
  }
  __attribute__((noinline)) R guarded(int mode) {
    if (RECOVER_SETJMP(mRecover.mJB) == 0) {
      g_slot[mHart] = &mRecover;
      bool r = physics(mode);
      g_slot[mHart] = 0;
      return r ? GR_TRUE : GR_FALSE;
    }
    return GR_JUMPED;
  }
  // the doTransition commit rule; returns the committed window
  Win transition(const Win& old, int mode, R* gr) {
    fast = old;
    *gr = guarded(mode);
    Win nw;
    if (*gr == GR_TRUE) {
      nw = fast;
    } else {
      nw = old;                  // pre-event window
      if (*gr == GR_JUMPED) nw.a[0] = 0;   // erase center
    }
    return nw;
  }
};
}  // namespace

int main() {
  Mini m{}, other{};
  m.mHart = 0;
  other.mHart = 1;
  Win old = {{5, 1, 2, 3, 4, 5, 6, 7}};
  Mini::R gr;

  Win w = m.transition(old, 0, &gr);
  check(gr == Mini::GR_TRUE && w.a[1] == 7 && w.a[0] == 5, "normal event commits fast window");
  check(g_slot[0] == 0, "slot clear after a normal event");

  w = m.transition(old, 1, &gr);
  check(gr == Mini::GR_JUMPED, "FAIL during physics reports a jump");
  check(m.mRecover.mCode == -77 && m.mRecover.mFileId == 12 && m.mRecover.mLine == 345,
        "code, file id and line recorded");
  check(g_slot[0] == 0, "slot cleared by the jump (this hart only)");
  check(w.a[0] == 0 && w.a[1] == 1 && w.a[2] == 2 && w.a[3] == 3 && w.a[4] == 4,
        "jump commits the OLD window with the center erased (scribbles gone)");

  w = m.transition(old, 2, &gr);
  check(gr == Mini::GR_FALSE && !memcmp(&w, &old, sizeof w),
        "false return commits the old window unchanged (half-modified fast copy dropped)");

  // RecoverSuspend: a FAIL inside a suspended scope falls through (hangs for real)
  m.mFellThrough = false;
  w = m.transition(old, 3, &gr);
  check(m.mFellThrough && gr == Mini::GR_TRUE, "FAIL inside RecoverSuspend does not recover");
  check(g_slot[0] == 0, "slot restored then cleared normally after the suspended scope");

  // per hart: hart 1 has no recovery point; its FAIL must NOT jump to hart 0's
  g_slot[0] = &m.mRecover;       // pretend hart 0 is mid-event
  other.mFellThrough = false;
  other.fail(-5, 0, 0);
  check(other.mFellThrough, "FAIL on a hart with no recovery point falls through");
  check(g_slot[0] == &m.mRecover, "other hart's slot untouched");
  g_slot[0] = 0;

  for (int i = 0; i < 20000; ++i) {
    w = m.transition(old, i % 3, &gr);
    if (g_bad) break;
  }
  check(g_slot[0] == 0, "20000 mixed events leave the slot clear");

  printf(g_bad ? "FAIL\n" : "physguard_test ok\n");
  return g_bad ? 1 : 0;
}
