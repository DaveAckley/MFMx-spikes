#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Define build directories
HOST_BUILD_DIR="build_host"
CROSS_BUILD_DIR="build_cross"
TOOLCHAIN_FILE="cross/toolchain-cross.cmake" # Ensure this path is correct
SHARED_GENERATED_HEADERS_DIR="generated_headers" # Must match CMakeLists.txt

echo ""
echo "--- Building crossmain in ${CROSS_BUILD_DIR} with ${TOOLCHAIN_FILE} ---"
mkdir -p "${CROSS_BUILD_DIR}"
cd "${CROSS_BUILD_DIR}"
cmake -DCMAKE_TOOLCHAIN_FILE=../${TOOLCHAIN_FILE} ../ # Configure for cross
make VERBOSE=1 -j$(nproc) # Build crossmain, use all available cores
#make -j$(nproc) # Build crossmain, use all available cores
cd ..

echo "--- Building hostmain in ${HOST_BUILD_DIR} ---"
mkdir -p "${HOST_BUILD_DIR}"
cd "${HOST_BUILD_DIR}"
cmake ../ # Configure for host
make VERBOSE=1 -j$(nproc) # Build hostmain, use all available cores
cd ..

echo ""
echo "--- Build process complete ---"
echo "Host executable: ${HOST_BUILD_DIR}/bin/hostmain"
echo "Cross executable: ${CROSS_BUILD_DIR}/bin/crossmain"
