
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"
#include "FastLocal.h"
#include "CrossUtils.h"
#include "HostBlock.h"

namespace MFM {
  static void reportFailureHostBlock(unsigned fileId, unsigned line) {
    extern HostBlock theHostBlock;
    HostBlock & hb = theHostBlock;
    u8 hart = fAll.mHartNum;
    hb.mPerHartFailFileID[hart] = (MFM::u16) fileId;
    hb.mPerHartFailFileLine[hart] = (MFM::u16) line;
  }
}

void DieHereNow(signed code, unsigned fileId, unsigned line) {
  if (fileId != 0u) MFM::reportFailureHostBlock(fileId, line);
  t6hang(code);
}
