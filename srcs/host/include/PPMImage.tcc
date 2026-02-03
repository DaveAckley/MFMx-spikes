/* -*- C++ -*- */
#include <stdio.h>

namespace MFM {
  template <u32 PIXWID,u32 PIXHEI>
  PPMImage<PIXWID,PIXHEI>::PPMImage() {
    snprintf((char *) mHeader,sizeof(mHeader),
             "P6\n"             // 3
             "%u %u\n"          // + DWID + 1 + DHEI + 1
             "255\n",           // + 4
             PIXWID, PIXHEI);
    reset();
  }

  template <u32 PIXWID,u32 PIXHEI>
  void PPMImage<PIXWID,PIXHEI>::reset() {
    memset_s(mRaster,'\0',sizeof(mRaster));
  }
}
