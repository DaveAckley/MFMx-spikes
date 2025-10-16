#include <iostream>
#include "host_header.h"
#include "shared_header.h"

extern int spikeMain();
extern int pymain();

int main() {
  std::cout << "Hello from hostmain!" << std::endl;
  host_function();
  shared_function();
  return pymain();
}
