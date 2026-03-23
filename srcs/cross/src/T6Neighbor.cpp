#include "T6Neighbor.h"
#include "Printf.h" // for DP

namespace MFM {
  ImageBlockAddr T6Neighbor::findIBAIfAny(BlockCode bc) const {
    ImageBlockAddr ret;
    ret.reset();                // ensure invalid
    ImageBlockHeader ngbibh = NRI3::blockingReadImageBlockHeader(mNoC0Us,mUsToNgbCT6Offset);
    if (ngbibh.isValid()) {
      for (u32 e = 0u; e < ngbibh.mEntries; ++e) {
        ImageBlockAddr iba = NRI3::blockingReadImageBlockAddr(mNoC0Us,mUsToNgbCT6Offset,e);
        if (!iba.isValid() || iba.mArrayLength==0) continue;
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
    bool usonbd = U8C::onBoardCT6Coord(usct6);
    /*
    DP.printf("T6NG uob%u u6=%u,%u off6=%d,%d\n",
              usonbd,
              usct6.x,usct6.y,
              ngbct6off.x,ngbct6off.y);
    */
    if (!usonbd) return false;
    U8C ngbct6 = usct6 + ngbct6off;
    bool nbonbd = U8C::onBoardCT6Coord(ngbct6);
    /*DP.printf("T6NG2 nob%u n6=%u,%u\n",
      nbonbd,ngbct6.x,ngbct6.y);*/
    if (!nbonbd) return false;
    mNoC0Ngb = U8C::makeNoC0CoordFromCT6Coord(ngbct6);
    return true;
  }
}

