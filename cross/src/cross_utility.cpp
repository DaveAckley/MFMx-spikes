#include <iostream> // Note: This might not be available on all embedded targets
#include "cross_header.h"

void cross_function() {
  // For embedded systems, you might replace std::cout with a UART print function
  // std::cout << "Cross specific function called." << std::endl;
}
