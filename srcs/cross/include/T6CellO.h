#pragma once /* -*- C++ -*- */

#include "CrossUtils.h" //before cross/memset_s and cross/strcmp
#include "CellBlock.h" 
#include "BlockCode.h" 

namespace MFM {
  // T6-side Cell operations
  struct T6CellO {
    u32 mCellBlockAddr;
    U8C mUsCT6;
    U8C mUsCellPos;
    U8C mCellNum;
    u8 mImageTypeIndex; //< in raster order, which count of our ImageType are we?
    bool init() ;
    u8 getXY(U8C cp) const ;
    U8C getXYofImage(ImageCode ic) const ;
    inline const CellBlock & getCB() const {
      return *(const CellBlock*) (u32*) mCellBlockAddr;
    }
    U8C getNoC0ofXY(U8C cp) const ;
  };
}
