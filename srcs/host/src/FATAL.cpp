#include "FATAL.h"
#include <stdexcept>

void DieHereNow(signed code,const char * file,unsigned line,const char * scode) {
  fprintf(stderr,"%s:%d: FATAL ERROR (%d): %s\n", file, line, code, scode);
  abort();
}
