####  WARNING  WARNING  WARNING  WARNING  WARNING
####
####   Written primarily by Gemini 2.5 Flash
####   with input from a first-time CMake user
####
####  WARNING  WARNING  WARNING  WARNING  WARNING

cmake_minimum_required(VERSION 3.15...4.0)
project(
  mfmx
#  VERSION 0.0.10
  LANGUAGES CXX ASM)

# Define the build directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# --- Include shared/CMakeLists.txt to get SHARED_SOURCES and SHARED_INCLUDE_DIRS ---
# This will process shared/CMakeLists.txt and make its PARENT_SCOPE variables available.
#message(STATUS "INCLUDING shared NOW")
include(${CMAKE_SOURCE_DIR}/srcs/shared/SharedDefinitions.cmake)
#message(STATUS "INCLUSION DONE, got ${SHARED_SOURCES} and ${SHARED_INCLUDE_DIRS}")

# SHARED_SOURCES and SHARED_INCLUDE_DIRS are now available here from shared/SharedDefinitions.cmake

# Define the shared generated headers directory ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")
# Ensure this directory exists before any build attempts
file(MAKE_DIRECTORY ${SHARED_GENERATED_HEADERS_DIR})

# Check if a toolchain file is being used (implies cross-compilation)
if (CMAKE_TOOLCHAIN_FILE)
  message(STATUS "Configuring for Cross-Compilation")
  add_subdirectory(srcs/cross)
else()
  message(STATUS "Configuring for Host build with scikit-core")

  # Host specific build
  set(PYBIND11_FINDPYTHON ON)
  set(pybind11_DIR ../../venv/lib/python3.10/site-packages/pybind11/share/cmake/pybind11)
  find_package(pybind11 CONFIG REQUIRED)

# Add the source subdirectory
add_subdirectory(srcs/host)

endif()
