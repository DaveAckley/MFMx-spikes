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

    ImageBlockAddr findIBAIfAny(NRI3 & nri3, BlockCode bc) const ;

    S8C mUsToNgbCT6Offset; //< ngb's CT6 pos relative to us
    U8C mNoC0Us;       //< our NoC0 coord
    U8C mNoC0Ngb;      //< our ngb's NoC coord?
  };
}

