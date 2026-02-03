/* -*- C++ -*- */
#include <stdio.h>

namespace MFM {
  template <u32 PIXWID,u32 PIXHEI>
  BGRImage<PIXWID,PIXHEI>::BGRImage() {
    reset();
  }

  template <u32 PIXWID,u32 PIXHEI>
  void BGRImage<PIXWID,PIXHEI>::reset() {
    memset_s(mRaster,'\0',sizeof(mRaster));
  }
}
