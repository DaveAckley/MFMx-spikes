#pragma once /* -*- C++ -*- */
#include "itype.h"
#include "U8C.h"
#include "S8C.h"
#include "NRIUtils.h"
#include "ImageBlock.h"

namespace MFM {
  struct T6Neighbor {

    T6Neighbor() { }

    bool init (U8C ournoc0c, S8C ngbct6off) ;

    bool isValid() const {
      return U8C::isNoC0CoordAT6(mNoC0Ngb);
    }

    ImageBlockAddr findIBAIfAny(BlockCode bc) const ;

    S8C mUsToNgbCT6Offset; //< ngb's CT6 pos relative to us
    U8C mNoC0Us;       //< our NoC0 coord
    U8C mNoC0Ngb;      //< our ngb's NoC coord?
  };

  struct T6NgbL1Block {
    T6Neighbor mT6Ngb;
    u32 mBaseAddress;
    u32 mItemSize;
    u32 mArrayLength;
    u32 mBlockSize;
    void reset() {
      memset_s(this, '\0', sizeof(*this));
    }
    bool isValid() const {
      return mT6Ngb.isValid() && mBlockSize > 0;
    }
    bool init(U8C ournoc0c, S8C ngbct6off, BlockCode bc) {
      reset();
      if (!mT6Ngb.init(ournoc0c, ngbct6off)) return false;
      ImageBlockAddr iba = mT6Ngb.findIBAIfAny(bc);
      DP.printf("T6NT u0(%u,%u) n6(%d,%d) v%d viba%d vngb%d(%u,%u) AL%d\n",
                ournoc0c.x, ournoc0c.y,
                ngbct6off.x, ngbct6off.y,
                mT6Ngb.isValid(),
                iba.isValid(),
                mT6Ngb.isValid(),
                mT6Ngb.mNoC0Ngb.x,
                mT6Ngb.mNoC0Ngb.y,
                iba.getArrayLength()
                );
      if (!iba.isValid()) return false;
      mBaseAddress = iba.mBlockAddr;
      mItemSize = getSizeFromBlockCode(bc);
      mArrayLength = iba.getArrayLength();
      mBlockSize = mItemSize * mArrayLength;
      return true;
    }
  };
}

