#include "S8C.h"
#include "U8C.h"
#include "Fail.h"

namespace MFM {
  S8C::S8C(U8C u) : x(u.x), y(u.y) {
    MFM_API_ASSERT(u.x <= S8_MAX && u.y <= S8_MAX, ILLEGAL_ARGUMENT);
  }
}
