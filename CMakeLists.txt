cmake_minimum_required(VERSION 3.10)
project(MixedBuild CXX)

# Define the build directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Include shared/CMakeLists.txt to get SHARED_SOURCES and SHARED_INCLUDE_DIRS ---
# This will process shared/CMakeLists.txt and make its PARENT_SCOPE variables available.
add_subdirectory(shared)

# SHARED_SOURCES and SHARED_INCLUDE_DIRS are now available here from shared/CMakeLists.txt

# --- NEW: Define the shared generated headers directory ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/generated_headers")
# Ensure this directory exists before any build attempts
file(MAKE_DIRECTORY ${SHARED_GENERATED_HEADERS_DIR})

# Check if a toolchain file is being used (implies cross-compilation)
if (CMAKE_TOOLCHAIN_FILE)
  message(STATUS "Configuring for Cross-Compilation")
  add_subdirectory(cross)
else()
  message(STATUS "Configuring for Host Build")
  add_subdirectory(host)
endif()
