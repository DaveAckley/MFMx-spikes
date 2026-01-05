#include "U8C.h"
#include "S8C.h"
#include "Fail.h"

namespace MFM {
  U8C::U8C(S8C s) : x(s.x), y(s.y) {
    MFM_API_ASSERT(s.x >= U8_MIN && s.y >= U8_MIN, ILLEGAL_ARGUMENT);
  }
}
