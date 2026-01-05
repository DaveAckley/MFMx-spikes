#include "U8C.h"
#include "S8C.h"

namespace MFM {
  U8C U8C::operator+(const S8C & s8) const {
    s32 sx = x + s8.x;
    s32 sy = y + s8.y;
    if (sx < 0 || sy < 0) FAIL(ILLEGAL_ARGUMENT);
    return U8C((u8) x, (u8) y);
  }

  bool U8C::addTo(const S8C & s8) {
    s32 sx = x + s8.x;
    s32 sy = y + s8.y;
    if (sx < 0 || sy < 0) return false; // this is unmodified
    x = (u8) sx; y = (u8) sy;
    return true;
  }
}
