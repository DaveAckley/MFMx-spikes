#include "Physics.h"
#include "FastT2.h" // for oneIn, between, etc
#include "HostBlock.h"

namespace MFM {

  extern HostBlock theHostBlock;

  static constexpr u32 PHY_DREG = 2u;
  static constexpr u32 PHY_RES = 3u;
  static constexpr u32 PHY_FB1 = 4u;
  static constexpr u32 PHY_FB4 = 5u;

  bool Physics::doTransition(EwpPayload & ewt) {
    bool ret = false;
    if (ewt.mPayloadState.mPayloadCode == EwpPayloadCode::EWPC_SOURCE_ONLY) {
      ++mEWsAttempted;
      ewL1ToFast(ewt.mOld);
      ret = doFastPhysics();
      ewFastToL1(ewt.mNew); // succeed or fail
      ewt.mPayloadState.mPayloadCode = EwpPayloadCode::EWPC_SOURCE_AND_DEST;
      if (ret) ++mEWsSucceeded;
      else ++mEWsFailed;

      if (mEWsAttempted%100 == 0) {
        HBPTAG(pewAt,mEWsAttempted);
        HBPTAG(pewSu,mEWsSucceeded);
      }
    } else if (ewt.mPayloadState.mPayloadCode != EwpPayloadCode::EWPC_EMPTY) {
      SNAP(2,HBPTAG(wotwot?,ewt.mPayloadState.mPayloadCode));
    } 
    return ret;
  }

  bool Physics::doFastPhysics() {
    HostBlock & hb = theHostBlock;
    EventWindow & ew = mFastEW;
    P4Atom & ca = ew.mAtoms[0];
    u32 cat = ca.getType();

    SNAP(200,HBXTAG(FIZIX,cat));

    ////// BEGIN "PHYSICS" //////
    switch (cat) {
    default: {
      // wth are you? 
      ca = P4Atom::makeEmptyAtom(); // buhbye now
      return true;
    }
    case P4Atom::EMPTY_TYPE: return true;
    case P4Atom::INACCESSIBLE_TYPE: return false;

    case P4Atom::START_TYPE: {
      //DP.printf("IAMI P4Atom::START_TYPE@0x%x\n",(u32) &ca);
      if (hb.mCommonArgs[1] != U16_MAX)
        ca = P4Atom::makeAtom((u16) hb.mCommonArgs[1]);
      else 
        ca = P4Atom::makeAtom(PHY_DREG);
      HBXTAG(STARTZO,ca.getType());
      return true;
    }

    case PHY_FB1: {
      u32 ngbsn = between(1u,4u);
      ew.mAtoms[ngbsn] = ca;
      return true;
    }

    case PHY_FB4: {
      ca.mStg[1]++;             // cheat and access underlying u32
      HBPTAG(FB4,ca.mStg[1]);
      for (u32 ngbsn = 1u; ngbsn <= 40u; ++ngbsn) { //MAX FB!
        P4Atom & na = ew.mAtoms[ngbsn];
        na = ca; // kaboom
      }
      return true;
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
