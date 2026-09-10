#pragma once  /* -*- C++ -*- */
#include "itype.h"

#include "ImageBlock.h"
#include "MDist.h"
#include "UxC.h"  // for U8C
#include "S8C.h"
#include "T6Neighbor.h"
#include "DefinedConstants.h" // for T6_IMAGE_BLOCK_ADDR

namespace MFM {

  struct T6CellO; // FORWARD

  struct T6ImageBlock {
    static ImageBlockHeader & getOurImageBlock() {
      return *(ImageBlockHeader*) (volatile u32*) T6_IMAGE_BLOCK_ADDR;
    }

    static bool findBlockCodeInCell(T6CellO & cello, bool skipUs, BlockCode bc,U8C & foundNoC0,ImageBlockAddr & iba) ;
    static bool findBlockCodeInCell(bool skipUs, BlockCode bc, U8C & foundNoC0,ImageBlockAddr & iba) ;

  };
}
