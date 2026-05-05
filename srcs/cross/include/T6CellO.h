#pragma once /* -*- C++ -*- */

#include "CrossUtils.h" //before cross/memset_s and cross/strcmp

#include "T6ImageBlock.h" 
#include "CellBlock.h" 
#include "Printf.h"

namespace MFM {
  // T6-side Cell operations
  struct T6CellO {
    u32 mOurCellBlockAddr; //< L1 address of our CellBlock
    U8C mUsCT6;         //< our CompactT6 coord (only T6s, in NoC0 order)
    U8C mCellPosCT6;    //< origin of our Cell in CT6 coords.
    U8C mUsCellPos;     //< our intra-Cell position (xy 0..cellStride.xy-1)
    U8C mCellNum;       //< our whole Cell's position (in Cell size units)
    u8 mImageTypeIndex; //< in raster order (and pretending the cell is complete!), which count of our ImageType are we?
    bool init() ;
    U8C getCellOriginCT6() const { //< get CT6 of cell {0,0}
      const CellBlock & cb = getOurCB();
      return mCellNum * cb.mCellStride + cb.mLayoutOffset; // XX + or - ?
    } 
    bool isValidCP(U8C cp) const {
      const CellBlock & cb = getOurCB();
      return cp.x < cb.mCellSize.x && cp.y < cb.mCellSize.y;
    }
    U8C getUsCT6() const { return mUsCT6; }//< get our CT6 pos
    u8 getImageCodeAtCellP(U8C cp) const ;    //< get ImageCode at intra-cell position cp
    U8C getCellPofImage(ImageCode ic) const ; //< get intra-cell pos of first-found ic, or (255,255)

    inline const CellBlock & getOurCB() const {
      return *(const CellBlock*) (u32*) mOurCellBlockAddr;
    }
    U8C getNoC0ofCellP(U8C cp) const ;
    U8C getNoC0ofUs() const { return getNoC0ofCellP(mUsCellPos); }

    Printer & to_repr(Printer& to) const ;
  };
}
