
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"
#include "FastLocal.h"
#include "CrossUtils.h"
#include "HostBlock.h"
#include "ExtraConstants.h"

namespace MFM {
  extern HostBlock theHostBlock;

  /// CONFIRM ExtraConstants.h decls
  static_assert(spOffHB==offsetof(HostBlock, mAtFailRegSP),"Bad SP offset");
  static_assert(raOffHB==offsetof(HostBlock, mAtFailRegRA),"Bad RA offset");
  static_assert(fpOffHB==offsetof(HostBlock, mAtFailRegFP),"Bad FP offset");
}

void PrepareToDie(signed code, unsigned fileId, unsigned line) {
  if (fileId != 0u) {
    using namespace MFM;
    HostBlock & hb = theHostBlock;
    u8 hart = fAll.mHartNum;
    hb.mPerHartFailFileID[hart] = (u16) fileId;
    hb.mPerHartFailFileLine[hart] = (u16) line;
    hb.mAtFailRegSP = getRegisterSP();
    hb.mAtFailRegRA = getRegisterRA();
    hb.mAtFailRegFP = getRegisterFP();
  }
  t6hangprep(code);
}

/*
extern "C" void bhang(void) {
  __asm__ __volatile__("1: j 1b" ::: "memory");
}
*/
