#pragma once  /* -*- C++ -*- */
#include "itype.h"

#include "ImageBlock.h"
#include "MDist.h"
#include "UxC.h"  // for U8C
#include "S8C.h"
#include "T6Neighbor.h"

namespace MFM {

  struct T6CellO; // FORWARD

  struct T6ImageBlock {
    static constexpr u32 IMAGE_BLOCK_ADDRESS = 0x14;

    static ImageBlockHeader & getOurImageBlock() {
      return *(ImageBlockHeader*) (volatile u32*) IMAGE_BLOCK_ADDRESS;
    }

    static bool findBlockCodeInCell(T6CellO & cello, bool skipUs, BlockCode bc,U8C & foundNoC0,ImageBlockAddr & iba) ;
    static bool findBlockCodeInCell(bool skipUs, BlockCode bc, U8C & foundNoC0,ImageBlockAddr & iba) ;

  };
}
