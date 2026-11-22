// physguard_test.cpp -- native test of PhysGuard (cross/images/ewp/include/
// PhysGuard.h) with libc setjmp: the arm / jump / disarm protocol that
// Physics::guardedPhysics and doTransition rely on, plus the recovery rule
// (a jump commits the OLD window with the center erased, an unarmed
// PHYSICS_FAIL dies).  The RISC-V setjmp itself is tested on hardware
// (t6sjlj, ERISC_NOTES 9.22).  Build/run:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -I../../cross/images/ewp/include -I../../shared/include physguard_test.cpp -o /tmp/pgt && /tmp/pgt
#define PHYSGUARD_HOSTTEST 1
#include <cstdio>
#include <cstring>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "PhysGuard.h"

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

// A stand-in for Physics: same guard shape, same recovery rule.
struct Mini {
  MFM::PhysGuard mGuard;
  Win fast;
  enum R { GR_FALSE, GR_TRUE, GR_JUMPED };

  __attribute__((noinline)) bool physics(int mode) {
    if (mode == 1) {             // scribble then fail, from one frame down
      for (u32 i = 1; i < 5; ++i) fast.a[i] = 99;
      mGuard.jumpOrDie(-77);
    }
    if (mode == 2) {             // physics declines (like an INACCESSIBLE center)
      fast.a[1] = 55;            // ...after touching the window
      return false;
    }
    fast.a[1] = 7;
    return true;
  }
  __attribute__((noinline)) R guarded(int mode) {
    if (setjmp(mGuard.mJB) == 0) {
      mGuard.arm();
      bool r = physics(mode);
      mGuard.disarm();
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
  Mini m;
  memset(&m, 0, sizeof m);
  Win old = {{5, 1, 2, 3, 4, 5, 6, 7}};
  Mini::R gr;

  Win w = m.transition(old, 0, &gr);
  check(gr == Mini::GR_TRUE && w.a[1] == 7 && w.a[0] == 5, "normal event commits fast window");
  check(!m.mGuard.isArmed(), "disarmed after a normal event");

  w = m.transition(old, 1, &gr);
  check(gr == Mini::GR_JUMPED, "PHYSICS_FAIL path reports a jump");
  check(m.mGuard.mCode == -77, "failure code recorded");
  check(!m.mGuard.isArmed(), "disarmed after a jump");
  check(w.a[0] == 0 && w.a[1] == 1 && w.a[2] == 2 && w.a[3] == 3 && w.a[4] == 4,
        "jump commits the OLD window with the center erased (scribbles gone)");

  w = m.transition(old, 2, &gr);
  check(gr == Mini::GR_FALSE && !memcmp(&w, &old, sizeof w),
        "false return commits the old window unchanged (half-modified fast copy dropped)");

  for (int i = 0; i < 20000; ++i) {
    w = m.transition(old, i % 3, &gr);
    if (g_bad) break;
  }
  check(!m.mGuard.isArmed(), "20000 mixed events leave the guard disarmed");

  // unarmed PHYSICS_FAIL must not return: the child dies of SIGILL/SIGTRAP
  pid_t pid = fork();
  if (pid == 0) {
    m.mGuard.disarm();
    m.mGuard.jumpOrDie(-5);
    _exit(0);                    // would mean it RETURNED: bad
  }
  int st = 0;
  waitpid(pid, &st, 0);
  check(WIFSIGNALED(st), "unarmed jumpOrDie dies (does not return)");

  printf(g_bad ? "FAIL\n" : "physguard_test ok\n");
  return g_bad ? 1 : 0;
}
