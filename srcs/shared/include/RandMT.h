#ifndef _RANDMT_H_
#define _RANDMT_H_

#include "mt19937.h"

namespace MFM {

class RandMT {
  mt_state mState;

  // MUST NOT PASS SEQUENTIAL SEEDS TO THIS VERSION
  void seedMT(uint32_t seed) { seed_initial_state(&mState, seed); }

public:
  RandMT() { seedMT(1u); }
  RandMT(uint32_t seed) { seedMT(seed); }

  inline uint32_t randomMT(void)
  {
    return random_uint32(&mState);
  }

  // ACKLEYHAX: This seeding function (NO LONGER) hacked for MFM
  void seedMT_MFM(uint32_t s) { seedMT(s); }

};

} /* namespace MFM */

#endif // _RANDMT_H_
