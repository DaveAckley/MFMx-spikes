cmake_minimum_required(VERSION 3.15...4.0)
project(c4i_mfm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(pybind11 REQUIRED)

# Define the build directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Host Build Configuration ---
add_subdirectory(host)

add_executable(c4i_mfm src/pymain.cpp)
target_link_libraries(c4i_mfm PRIVATE pybind11::embed)

# --- Cross-Compilation Configuration ---
# You'll need to define your cross-compiler toolchain file.
# For example, create a file named 'toolchain-cross.cmake' in your project root.
# A basic example is provided below.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchain-cross.cmake")
add_subdirectory(cross)
