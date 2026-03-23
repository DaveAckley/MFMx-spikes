
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"
#include "FastLocal.h"
#include "CrossUtils.h"

void DieHereNow(signed code,const char * file,unsigned line) {
  file = MFM::stripDirs(file);
  MFM::DP.printf("\n%s:%d:(%d,%d,%s) DIES %d\n",file,line,
                 MFM::fAll.mPos.x,MFM::fAll.mPos.y,
                 MFM::hartName(MFM::fAll.mHartNum),code);    // try to leave a corpse in hostbuffer
  t6hang(code);
}
