#include "T6Neighbor.h"
#include "Printf.h" // for DP
#include "Debug.h"

namespace MFM {
  ImageBlockAddr T6Neighbor::findIBAIfAny(BlockCode bc, bool debug) const {
    HBASSERT_EQ(isValid(),true);
    //if (debug) HBNOTE("CALT");
    ImageBlockAddr ret;
    ret.reset();                // ensure invalid
    ImageBlockHeader ngbibh = NRI3::blockingReadImageBlockHeaderCT6Offset(mNoC0Us,mUsToNgbCT6Offset,debug);
    if (!ngbibh.isValid()) {
      if (debug) HBNOTE("INVG");
    } else {
      if (debug) HBPVAL(ngbibh.mEntries);
      for (u32 e = 0u; e < ngbibh.mEntries; ++e) {
        ImageBlockAddr iba = NRI3::blockingReadImageBlockAddrCT6Offset(mNoC0Us,mUsToNgbCT6Offset,e,debug);
        //HBPVAL(e);
        if (!iba.isValid() || iba.mStorageCount==0) {
          if (debug) {
            HBPVAL(mNoC0Us);
            HBPVAL(mUsToNgbCT6Offset);
            HBNOTE("NVALG");
            HBNOTE(iba.isValid());
            HBNOTE(iba.mStorageCount);
            HBPVAL(e);
          }
          continue;
        }
        if (iba.getBlockCode() == bc) {
          ret = iba;            // ret becomes valid
          if (debug) HBPVAL(iba.mStorageCount);
          break;
        }
      }
    }
    if (debug && !ret.isValid()) HBPVAL(ret.isValid());
    return ret;                 // invalid or not..
  }

#if 0
  s32 T6Neighbor::forIBAs(NRI3 & nri3, VisitIBA visitor, void * arg) const {
    ImageBlockHeader hubibh = nri3.blockingReadImageBlockHeaderCT6Offset(hb,tohub);
    if (!hubibh.isValid()) return -1;
    for (u32 e = 0u; e < hubibh.mEntries; ++e) {
      ImageBlockAddr iba = fB.mNRI3.blockingReadImageBlockAddr(hb,tohub,e);
      if (!iba.isValid()) return -2;
      if (visitor(iba,arg)) return 1;
    }
    return 0;
  }
#endif

  bool T6Neighbor::init(U8C ournoc0c, S8C ngbct6off) {
    mNoC0Ngb.reset();         // Assume blown
    mUsToNgbCT6Offset = ngbct6off;
    mNoC0Us = ournoc0c;
    U8C usct6 = U8C::makeCT6CoordFromNoC0Coord(ournoc0c);
    bool usonbd = U8C::onBoardCT6Coord(usct6);

    if (!usonbd) return false;
    U8C ngbct6(usct6.x + ngbct6off.x,usct6.y + ngbct6off.y);
    bool nbonbd = U8C::onBoardCT6Coord(ngbct6);

    if (!nbonbd) return false;
    mNoC0Ngb = U8C::makeNoC0CoordFromCT6Coord(ngbct6);
    return true;
  }
}

