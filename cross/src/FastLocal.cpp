#include "FastLocal.h"

// See cross/src/_BUD.ld.in for section defs and such

#define FAST_LOCAL(typedecl,name,hart)                         \
  typedecl                                                     \
  name                                                         \
  __attribute__ ((section(XSTR_MACRO(CONC(.fastram_,hart)))))  \

namespace MFM {
  // Everybody gets their own copy of FastAll
  FAST_LOCAL(FastAll,fAll,all);

  // All these are unique per-hart
  FAST_LOCAL(FastB,fB,b);
  FAST_LOCAL(FastT0,fT0,t0);
  FAST_LOCAL(FastT1,fT1,t1);
  FAST_LOCAL(FastT2,fT2,t2);
  FAST_LOCAL(FastNC,fNC,nc);
}
