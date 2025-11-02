#include "cross_header.h"
#include "shared_header.h"

int main() {
  // For embedded systems, you might replace std::cout with a UART print function
  // std::cout << "Hello from crossmain!" << std::endl;
  cross_function();
  shared_function();
  return 0;
}
