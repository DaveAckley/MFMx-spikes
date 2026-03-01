#pragma once /* -*- C++ -*- */

#include "CrossUtils.h" //before cross/memset_s and cross/strcmp

#include "T6ImageBlock.h" 
#include "CellBlock.h" 

namespace MFM {
  // T6-side Cell operations
  struct T6CellO {
    u32 mOurCellBlockAddr; //< L1 address of our CellBlock
    U8C mUsCT6;         //< our CompactT6 coord (only T6s, in NoC0 order)
    U8C mCellPosCT6;    //< origin of our Cell in CT6 coords.
    U8C mUsCellPos;     //< our intra-Cell position (xy 0..cellStride.xy-1)
    U8C mCellNum;       //< our whole Cell's position (in Cell size units)
    u8 mImageTypeIndex; //< in raster order, which count of our ImageType are we?
    bool init() ;
    u8 getImageCodeAtXY(U8C cp) const ;    //< get ImageCode at intra-cell position cp
    U8C getXYofImage(ImageCode ic) const ; //< get intra-cell pos of first-found ic, or (255,255)
    inline const CellBlock & getOurCB() const {
      return *(const CellBlock*) (u32*) mOurCellBlockAddr;
    }
    U8C getNoC0ofXY(U8C cp) const ;
  };
}
