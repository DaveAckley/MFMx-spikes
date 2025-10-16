#include <iostream>
#include "host_header.h"
#include "shared_header.h"

int main() {
  std::cout << "Hello from hostmain!" << std::endl;
  host_function();
  shared_function();
  return 0;
}
