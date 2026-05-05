#pragma once  /* -*- C++ -*- */

#include "itype.h"
#include <string>
#include "UxC.h"
#include "S8C.h"
#include "XUtils.h" // for memset_s

namespace MFM {
  struct CellBlock {
    static constexpr u32 MAP_AREA = 56u;
    u8 mCellType;
    U8C mCellSize;
    S8C mLayoutOffset;
    U8C mCellStride;
    u8 mImageMap[MAP_AREA];
    u8 mChecksum;

    std::string reportCellBlock() const {
      std::string ret = "<CellBlock";
      ret += "#" + std::to_string((u32) mCellType);
      ret += ",sz=" +mCellSize.to_repr();
      ret += ",lo=" +mLayoutOffset.to_repr();
      ret += ",st=" +mCellStride.to_repr();
      ret += ">";
      return ret;
    }

    void init() {
      memset_s(this, '\0', sizeof(*this));
    }

    u8 getCellType() const { return mCellType; }
    void setCellType(u8 ct) { mCellType = ct; }

    void setCellSize(U8C size) {
      MFM_API_ASSERT(size.x*size.y > 0, ILLEGAL_ARGUMENT);
      MFM_API_ASSERT(size.x*size.y <= MAP_AREA, OUT_OF_ROOM);
      mCellSize = size;
    }
    U8C getCellSize() const { return mCellSize; }

    void setCellStride(U8C stride) {
      MFM_API_ASSERT(stride.x >= mCellSize.x, ILLEGAL_ARGUMENT);
      MFM_API_ASSERT(stride.y >= mCellSize.y, ILLEGAL_ARGUMENT);
      mCellStride = stride;
    }
    U8C getCellStride() const { return mCellStride; }

    void setLayoutOffset(S8C layoutoffset) {
      mLayoutOffset = layoutoffset; //checks? check check checking no checks?
    }
    S8C getLayoutOffset() const { return mLayoutOffset; }

    const u8 * getImageAddress(U8C cellpos) const {
      if (cellpos.x >= mCellSize.x) return 0;
      if (cellpos.y >= mCellSize.y) return 0;
      return &mImageMap[cellpos.y * mCellSize.x + cellpos.x];
    }
    u8 * getImageAddress(U8C cellpos) {
      if (cellpos.x >= mCellSize.x) return 0;
      if (cellpos.y >= mCellSize.y) return 0;
      return &mImageMap[cellpos.y * mCellSize.x + cellpos.x];
    }

    u8 getImageAtCellPos(U8C cellpos, u8 missingval = 0xff) const {
      const u8 * p = getImageAddress(cellpos);
      return p ? *p :  missingval;
    }

    bool setCellPos(U8C cellpos, u8 imagecode) {
      u8 * p = getImageAddress(cellpos);
      if (!p) return false;
      *p = imagecode;
      return true;
    }

    u8 computeChecksum() const { // lame-o 7bit checksum
      u32 sum = 0u;
      const u8 *b = (u8*) this;
      const u8 *p = b;
      const u8 *e = b + sizeof(CellBlock) - 1u; // don't sum the sum
      while (p < e) sum = (sum<<1) + *p++ + (sum>>31);
      return ((u8) (sum + (sum>>8) + (sum>>16) + (sum>>24)))|1u; // 1u: gtee all zeros fails
    }

    void setChecksum() { mChecksum = computeChecksum(); }
    bool checkChecksum() const { return mChecksum == computeChecksum(); }
    bool isValid() const { return checkChecksum(); }

  };
  extern CellBlock theCellBlock[1]; //< only resolves under cross/ ? Shouldn't be here?
}

