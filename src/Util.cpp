#include "Util.h"
#include <memory>
#include <fstream>
#include <vector>
#include <iostream>
#include <string>

std::string readFile(const char * path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate); // Open in binary mode and at end

  if (!file.is_open()) 
    FATAL("Failed to open file: %s", path);

  std::streamsize rvCodeSize = file.tellg(); 
  file.seekg(0, std::ios::beg); // Seek back to beginning

  auto rvcode = std::make_unique<char[]>(rvCodeSize);

  if (!file.read(rvcode.get(), rvCodeSize))
    FATAL("Failed to read file: %s", path);

  file.close();

  return std::string(rvcode.get(), rvCodeSize);
    
}
