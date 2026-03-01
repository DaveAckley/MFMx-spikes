#include "T6Neighbor.h"
#include "Printf.h" // for DP

namespace MFM {
  ImageBlockAddr T6Neighbor::findIBAIfAny(NRI3 & nri3, BlockCode bc) const {
    ImageBlockAddr ret;         // invalid since not initted
    ImageBlockHeader hubibh = nri3.blockingReadImageBlockHeader(mNoC0Us,mUsToNgbCT6Offset);
    if (hubibh.isValid()) {
      for (u32 e = 0u; e < hubibh.mEntries; ++e) {
        ImageBlockAddr iba = nri3.blockingReadImageBlockAddr(mNoC0Us,mUsToNgbCT6Offset,e);
        if (!iba.isValid()) continue;
        if (iba.getBlockCode() == bc) {
          ret = iba;            // ret becomes valid
          break;
        }
      }
    }
    return ret;                 // invalid or not..
  }

#if 0
  s32 T6Neighbor::forIBAs(NRI3 & nri3, VisitIBA visitor, void * arg) const {
    ImageBlockHeader hubibh = nri3.blockingReadImageBlockHeader(hb,tohub);
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
    if (!U8C::onBoardCT6Coord(usct6)) return false;
    DP.printf("T6NG (%u,%u) (%d,%d)\n",
              usct6.x,usct6.y,
              ngbct6off.x,ngbct6off.y);
    U8C ngbct6 = usct6 + ngbct6off;
    if (!U8C::onBoardCT6Coord(ngbct6)) return false;
    mNoC0Ngb = U8C::makeNoC0CoordFromCT6Coord(ngbct6);
    return true;
  }
}

