# CMakeLists.txt (top level)
cmake_minimum_required(VERSION 3.18...3.27)
project(mfmx LANGUAGES CXX)

# Find pybind11
find_package(pybind11 CONFIG REQUIRED)

# Add the source subdirectory
add_subdirectory(src)

