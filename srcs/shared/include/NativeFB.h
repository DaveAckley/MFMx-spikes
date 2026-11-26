#pragma once  /* -*- C++ -*- */
/* NativeFB.h -- the native FB1 and FB4 physics (PHY_FB1 = 4, PHY_FB4 = 5),
   moved out of srcs/cross/images/ewp/src/Physics.cpp unchanged in
   behavior so that the host test (srcs/host/tools/bytephys_test.cpp) can
   run the very same code against the bytecode versions
   (scripts/bytephys/fb1.bp, fb4.bp; BytePhys.h).

   Env (template parameter) provides:
     P4Atom & atom(u32 sn)          site sn of the window (0 = center)
     u32 between(u32 lo, u32 hi)    uniform in [lo, hi]
     bool oneIn(u32 n)
     void splat()                   FB1's 'old enough to drink' moment
                                    (the EWP logs SPLAT! once)
*/
#include "itype.h"
#include "P4Atom.h"

namespace MFM {

  template <class Env>
  bool nativeFB1(Env & env) {
    P4Atom & ca = env.atom(0u);
    ca.mStg[1]++;               // cheat and access underlying u32
    if (ca.mStg[0] > 0) {
      if (--ca.mStg[0] == 0) {
        for (u32 ngbsn = 0u; ngbsn <= 40; ++ngbsn) {
          if (env.oneIn(100)) continue; // 1% survives
          env.atom(ngbsn) = P4Atom::makeEmptyAtom(); // kabam
        }
        return true;
      } else env.atom(env.between(5u, 8u)) = ca; // extra repro shot (Moore!)
    } else if (ca.mStg[1] >= 21 && env.oneIn(100'000)) { // old enough
      env.splat();
      ca.mStg[0] = env.between(5, 500);
      ca.mStg[1] += env.between(1'000, 10'000); // jump in color space too
    }

    u32 ngbsn = env.between(1u, 4u);
    env.atom(ngbsn) = ca;
    return true;
  }

  template <class Env>
  bool nativeFB4(Env & env) {
    P4Atom & ca = env.atom(0u);
    ca.mStg[1]++;               // cheat and access underlying u32
    for (u32 ngbsn = 1u; ngbsn <= 40u; ++ngbsn) //MAX FB!
      env.atom(ngbsn) = ca;     // kaboom
    return true;
  }
}
