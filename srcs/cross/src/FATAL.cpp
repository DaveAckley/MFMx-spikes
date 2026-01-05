
#include "FATAL.h"
#include "itype.h"
#include "Printf.h"

void DieHereNow(signed code,const char * file,unsigned line) {
  MFM::DP.printf("\n%s:%d:FAIL%d\n",file,line,code); // try to leave a corpse in hostbuffer
  t6hang(code);
}
