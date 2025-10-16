#include <iostream>
#include "host_header.h"
#include "shared_header.h"

#if 0
extern int spikeMain();
extern "C++" int pymain();

int main() {
  std::cout << "Hello from hostmain!" << std::endl;
  host_function();
  shared_function();
  return pymain();
}
#endif
