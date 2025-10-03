#include "FATAL.h"
#include "itype.h"

void DieHereNow(signed code,const char * file,unsigned line) {
  t6hang(code);
}
