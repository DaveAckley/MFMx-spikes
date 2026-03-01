#include "T6CellO.h"
#include "T6ImageBlock.h"
#include "FastLocal.h"  // for fAll

namespace MFM {
  bool T6CellO::init() {
    memset_s(this,'\0',sizeof(*this));
    // Find our cellblock from the outside (whyyyy?)
    ImageBlockHeader & ib = T6ImageBlock::getTheImageBlock();
    ImageBlockAddr cba = ib.findIBAIfAny(BlockCode::BC_CELLBLOCK);
    if (cba.mBlockCode != BlockCode::BC_CELLBLOCK) return false;

    mOurCellBlockAddr = cba.mBlockAddr;
    const CellBlock & cb = getOurCB();
    if (!cb.isValid()) return false;
    
    U8C usnoc = fAll.mPos;
    mUsCT6 = U8C::makeCT6CoordFromNoC0Coord(usnoc);
    mCellPosCT6 = mCellNum * cb.mCellStride;
    mCellNum = mUsCT6 / cb.mCellStride;
    mUsCellPos = mUsCT6 % cb.mCellStride;

    u8 ourimagecode = ib.getImageCode();
    u8 count = 0u;
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C atc(x,y);
        if (mUsCellPos == atc) {
          mImageTypeIndex = count;
          return true;
        }
        if (getImageCodeAtXY(atc) == ourimagecode) ++count;
      }
    }

    return false; // failed to find ourselves?
  }

  u8 T6CellO::getImageCodeAtXY(U8C cp) const {
    const CellBlock & cb = getOurCB();
    return cb.getCellPos(cp, 0u);
  }

  U8C T6CellO::getXYofImage(ImageCode ic) const {
    const CellBlock & cb = getOurCB();
    for (u8 y = 0u; y < cb.mCellSize.y; ++y) {
      for (u8 x = 0u; x < cb.mCellSize.x; ++x) {
        U8C at(x,y);
        if (getImageCodeAtXY(at) == ic) return at;
      }
    }
    return {U8_MAX,U8_MAX};
  }

  U8C T6CellO::getNoC0ofXY(U8C cp) const {
    const CellBlock & cb = getOurCB();
    U8C cellct6 = mCellNum * cb.mCellStride;
    U8C xyct6 = cellct6 + cp;
    return U8C::makeNoC0CoordFromCT6Coord(xyct6); // 255,255 if no noc for cp
  }
}
