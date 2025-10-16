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
include(${CMAKE_SOURCE_DIR}/shared/SharedDefinitions.cmake)
#message(STATUS "INCLUSION DONE, got ${SHARED_SOURCES} and ${SHARED_INCLUDE_DIRS}")

# SHARED_SOURCES and SHARED_INCLUDE_DIRS are now available here from shared/SharedDefinitions.cmake

# Define the shared generated headers directory ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/generated_headers")
# Ensure this directory exists before any build attempts
file(MAKE_DIRECTORY ${SHARED_GENERATED_HEADERS_DIR})

# Check if a toolchain file is being used (implies cross-compilation)
if (CMAKE_TOOLCHAIN_FILE)
  message(STATUS "Configuring for Cross-Compilation")
  add_subdirectory(cross)
else()
  message(STATUS "Configuring for Host build with scikit-core")

  # Host specific build
  set(PYBIND11_FINDPYTHON ON)
  set(pybind11_DIR ../../venv/lib/python3.10/site-packages/pybind11/share/cmake/pybind11)
  find_package(pybind11 CONFIG REQUIRED)

#  python_add_library(_MFMx MODULE WITH_SOABI
#    host/src/pybindings.cpp
#  )

# Add the source subdirectory
add_subdirectory(host)


  # target_sources(_MFMx PRIVATE
  #   host/src/spikeMain.cpp
  #   host/src/BlackHole.cpp
  #   host/src/C4I.cpp
  #   host/src/CodeManager.cpp
  #   host/src/FATAL.cpp
  #   host/src/FailStrings.cpp
  #   host/src/OurTLBs.cpp
  #   ${SHARED_SOURCES}
  # )

  # target_include_directories(_MFMx PRIVATE
  #   ${CMAKE_CURRENT_SOURCE_DIR}/include
  #   ${CMAKE_CURRENT_SOURCE_DIR}/host/include
  #   ${SHARED_INCLUDE_DIRS}
  #   ${CMAKE_SOURCE_DIR}/generated_headers
  # )

  # # Add C++ compiler options specifically for pymain ---
  # target_compile_options(_MFMx PRIVATE
  #   -g          # Add debug symbols
  #   #    -O0         # Disable optimizations (often desired for debugging)
  #   #  -Wall       # Enable all common warnings
  #   #  -Wextra     # Enable extra warnings
  #   -Wno-unused-parameter  # stuff we want
  #   -Wno-unused-variable   # to live with
  #   # Add any other host-specific C++ flags here
  # )

  
endif()
