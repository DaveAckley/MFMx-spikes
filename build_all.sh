#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Define build directories
HOST_BUILD_DIR="build_host"
CROSS_BUILD_DIR="build_cross"
TOOLCHAIN_FILE="cross/toolchain-cross.cmake" # Ensure this path is correct
SHARED_GENERATED_HEADERS_DIR="generated_headers" # Must match CMakeLists.txt

DIR=$(dirname $(readlink -f $0))
CUR=`pwd`
echo ${CUR}
if [ "x${CUR}" != "x${DIR}" ] ; then
    echo "ONLY RUN THIS SCRIPT FROM '${DIR}' not '${CUR}'"
    exit 2
fi
if [ "x$1" == "xnuke" ]; then
    shift
    echo -n REALCLEANING---
    rm -rf ${DIR}/${HOST_BUILD_DIR} ${DIR}/${CROSS_BUILD_DIR} ${DIR}/${SHARED_GENERATED_HEADERS_DIR}
    echo DONE
fi

if [ "x$1" == "xtest" ]; then
    TESTFILE="$2"
    shift
    shift
fi

if [ "x$1" != "x" ]; then
    echo "BAD ARG BAD '$1'"
    exit 3
fi

echo ""
echo "--- Building crossmain in ${CROSS_BUILD_DIR} with ${TOOLCHAIN_FILE} ---"
mkdir -p "${CROSS_BUILD_DIR}"
cd "${CROSS_BUILD_DIR}"
cmake -DCMAKE_TOOLCHAIN_FILE=../${TOOLCHAIN_FILE} ../ # Configure for cross
make VERBOSE=1 -j$(nproc) # Build crossmain, use all available cores
#make -j$(nproc) # Build crossmain, use all available cores
cd ..

echo "--- Building hostmain in ${HOST_BUILD_DIR} VIA PIP ---"
PYTHON="/data/ackley/PART4/code/D/blackholeSpikes/venv/bin/python"
WHEELS_DIR="/data/ackley/PART4/code/D/blackholeSpikes/wheels"
#${PYTHON} -m pip --verbose wheel --wheel-dir=${WHEELS_DIR} .
#${PYTHON} -m pip --verbose install --no-index --find-links=${WHEELS_DIR} .
#${PYTHON} -m pip --verbose install --find-links=${WHEELS_DIR} .
${PYTHON} -m pip install --find-links=${WHEELS_DIR} .
#mkdir -p "${HOST_BUILD_DIR}"
#cd "${HOST_BUILD_DIR}"
#cmake ../ # Configure for host
#make VERBOSE=1 -j$(nproc) # Build hostmain, use all available cores
#cd ..

echo ""
echo "--- Build process complete ---"

if [ "x${TESTFILE}" != "x" ] ; then
    echo "--- Resetting before test"
    tt-smi -r
    echo "--- Running test file: ${TESTFILE}"
    ${TESTFILE}
    echo "--- Test file ${TESTFILE}: Done"
fi
