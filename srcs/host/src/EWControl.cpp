#include "EWControl.h"
#include "Fail.h"
#include <time.h>     /* For time() */
#include "MDist.h"

namespace MFM {
  EWControl theEWControl;

  EWControl & EWControl::getTheEWControl() {
    return theEWControl;
  }

  EWControl::EWControl() : mRandom(time(NULL))
  { initGrid(); }

  void EWControl::initGrid() {
    mEWsActive = false;
    mMin = {-8,-8};
    mMax = {+8,+8};
    memset_s(mGrid, 0u, sizeof(mGrid));

    S32C sc = randomCoordInBounds();
    setAtom(sc,P4Atom::makeStartAtom());
  }

  S32C EWControl::randomCoordInBounds() {
    s32 x = mRandom.Between(mMin.x,mMax.x); // note with center in bounds,
    s32 y = mRandom.Between(mMin.y,mMax.y); // ew may reach beyond bounds
    return { x, y };
  }

  bool EWControl::pickEWCenter(S32C & occupied) {
    u32 area = (mMax.x - mMin.x)*(mMax.y - mMin.y);
    // try sampling for a while
    for (u32 i = 0u; i < area/5u; ++i) {
      S32C c = randomCoordInBounds();
      u16 t = getAtom(c).getType();
      if (t != P4Atom::EMPTY_TYPE &&
          t != P4Atom::INACCESSIBLE_TYPE) {
        occupied = c;
        return true;
      }
    }
    // fall back to enumeration
    u32 count = 0u;
    for (s32 x = mMin.x; x <= mMax.x; ++x) {
      for (s32 y = mMin.y; y <= mMax.y; ++y) {
        S32C c(x,y);
        u16 t = getAtom(c).getType();
        if (t != P4Atom::EMPTY_TYPE &&
            t != P4Atom::INACCESSIBLE_TYPE &&
            mRandom.OneIn(++count))
          occupied = c;
      }
    }
    return count > 0u;
  }

  typedef MDist<4> MDist4;

  void EWControl::fillEW(S32C center, EventWindow & ew) {
    const MDist4 & md = MDist4::get();
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.GetPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      ew.mAtoms[sn] = a;
    }
  }

  bool EWControl::tryLoadEWCar(EWCarStorage::EWCar & ec) {
    S32C ctr;
    if (!pickEWCenter(ctr)) return false; // ah fudge
    EWBlock & eb = ec.getContent();
    eb.mHiddenXPos = ctr.x;
    eb.mHiddenYPos = ctr.y;
    fillEW(ctr,eb.mOld);
    eb.mNew.reset();
    return true;
  }

  bool EWControl::commitEWIfPossible(BHTag tag, S32C center, EventWindow & oldew, EventWindow & newew) {
    const MDist4 & md = MDist4::get();
    for (u32 sn = 0u; sn < 41u; ++sn) {
      SPoint c = md.GetPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      P4Atom a = getAtom(at);
      if (a != oldew.mAtoms[sn]) return false;
    }

    for (u32 sn = 0u; sn < 41u; ++sn) {
      if (oldew.mAtoms[sn] == newew.mAtoms[sn]) continue;
      P4Atom a = newew.mAtoms[sn];

      SPoint c = md.GetPoint(sn);
      S32C at = { center.x+c.GetX(), center.y+c.GetY() };
      if (setAtom(at, a) && a.getType() != P4Atom::EMPTY_TYPE) {
        if (at.x < mMin.x) mMin.x = at.x;
        if (at.x > mMax.x) mMax.x = at.x;
        if (at.y < mMin.y) mMin.y = at.y;
        if (at.y > mMax.y) mMax.y = at.y;
      }
    }
    return true;
  }
}
