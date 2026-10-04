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

  extern const u8 BITMAP_FONT_5X7[95][5];

  template <u32 PIXWID,u32 PIXHEI>
  void BGRImage<PIXWID,PIXHEI>::drawChar(U16C at, u8 ch, RGBPix fg, RGBPix bg, u8 scale) {
    if (ch < 32 || ch > 126) return;
    const u8* glyph = BITMAP_FONT_5X7[ch - 32];
    for (u8 col = 0; col < 4+scale; ++col) {
      for (u8 row = 0; row < 7; ++row) {
        for (u8 sx = 0; sx < scale; ++sx) {
          for (u8 sy = 0; sy < scale; ++sy) {
            if (col < 5 && (glyph[col] & (1 << row)))
              setPixel(at + U16C(scale*col+sx,scale*row+sy), fg);
            else if (fg != bg)
              setPixel(at + U16C(scale*col+sx,scale*row+sy), bg);
          }
        }
      }
    }
  }  

}
