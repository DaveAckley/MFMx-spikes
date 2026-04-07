#include "T6CellO.h"
#include "T6ImageBlock.h"
#include "FastLocal.h"  // for fAll
#include "Debug.h"

namespace MFM {
  bool T6CellO::init() {
    //    HBMARK;
    memset_s(this,'\0',sizeof(*this));
    // Find our cellblock from the outside (whyyyy?)
    ImageBlockHeader & ib = T6ImageBlock::getOurImageBlock();
    ImageBlockAddr cba = ib.findIBAIfAny(BlockCode::BC_CELLBLOCK);
    //HBMARK;
    if (cba.mBlockCode != BlockCode::BC_CELLBLOCK) return false;

    mOurCellBlockAddr = cba.mBlockAddr;
    const CellBlock & cb = getOurCB();
    //HBPVAL(mOurCellBlockAddr);
    if (!cb.isValid()) return false;
    
    U8C usnoc = fAll.mNoC0;
    //HBPVAL(usnoc);
    mUsCT6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    //HBPVAL(mUsCT6);
    mCellNum = mUsCT6 / cb.mCellStride;
    mCellPosCT6 = mCellNum * cb.mCellStride;
    mUsCellPos = mUsCT6 % cb.mCellStride;

    u8 ourimagecode = ib.getImageCode();
    HBPVAL(ourimagecode);
    u8 count = 0u;
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C atcp = U8C(x,y); //intra-cell coord
        U8C atct6 = atcp + mCellPosCT6; // CT6 coord
        //HBPVAL(atct6);

        if (mUsCT6 == atct6) {
          HBPVAL(atct6);
          HBPVAL(count);
          mImageTypeIndex = count;
          return true;
        }
        if (getImageCodeAtCellP(atcp) == ourimagecode) ++count;
      }
    }
    //HBMARK;
    return false; // failed to find ourselves?
  }

  u8 T6CellO::getImageCodeAtCellP(U8C cp) const {
    const CellBlock & cb = getOurCB();
    return cb.getImageAtCellPos(cp, 0u);
  }

  U8C T6CellO::getCellPofImage(ImageCode ic) const {
    const CellBlock & cb = getOurCB();
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C at(x,y);
        if (getImageCodeAtCellP(at) == ic) return at;
      }
    }
    return {U8_MAX,U8_MAX};
  }

#if 0 
  U8C T6CellO::findCellPOfferingBlockCode(BlockCode ic) const {
    const CellBlock & cb = getOurCB();
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C at(x,y);
        if (getImageCodeAtCellP(at) == ic) return at;
      }
    }
    return {U8_MAX,U8_MAX};
  }
#endif

  U8C T6CellO::getNoC0ofCellP(U8C cp) const {
    const CellBlock & cb = getOurCB();
    U8C cellct6 = mCellNum * cb.mCellStride;
    U8C xyct6 = cellct6 + cp;
    return U8C::makeNoC0CoordFromCT6Coord(xyct6); // 255,255 if no noc for cp
  }

  Printer & T6CellO::to_repr(Printer& to) const {
    to.printf("<T6Cello ucba=%u u6=%u,%u co6=%u,%u ucp=%u,%u idx=%u>\n",
              mOurCellBlockAddr,
              mUsCT6.x,mUsCT6.y,
              mCellPosCT6.x,mCellPosCT6.y,
              mUsCellPos.x,mUsCellPos.y,
              mImageTypeIndex);
    return to;
  }
}
