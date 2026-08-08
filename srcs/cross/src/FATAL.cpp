
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"
#include "FastLocal.h"
#include "CrossUtils.h"
#include "HostBlock.h"

namespace MFM {
  extern HostBlock theHostBlock;
}

void DieHereNow(signed code, unsigned fileId, unsigned line) {
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
  volatile MFM::u8 flag[] = "IDIEHERENOW.";
  t6hang(code);
}
