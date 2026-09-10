#include "S8C.h"
#include "UxC.h" // for U8C
#include "Fail.h"

namespace MFM {
  S8C::S8C(U8C u) : x(u.x), y(u.y) {
    MFM_API_ASSERT(u.x <= S8_MAX && u.y <= S8_MAX, ILLEGAL_ARGUMENT);
  }

  bool S8C::toU8C(U8C& u) {
    if (x < 0 || y < 0) return false;
    u = U8C(x,y);
    return true;
  }

  U8C S8C::abs() const {
    U8C ret;
    ret.x = (u8) (x < 0 ? -x : x);
    ret.y = (u8) (y < 0 ? -y : y);
    return ret;
  }

}
