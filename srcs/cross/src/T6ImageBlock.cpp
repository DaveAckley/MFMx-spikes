#include "T6ImageBlock.h"

namespace MFM {
  ImageBlockHeader T6ImageBlock::readHeader(T6Neighbor & ctn) {
    ImageBlockHeader ret; // not yet initted => invalid
    if (!ctn.isValid()) return ret;
    FAIL(INCOMPLETE_CODE);
  }

}

    
