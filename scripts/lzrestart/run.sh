#!/bin/bash
# Host test of lzmfmx reset()/EOF restartability (no hardware, no pybind).
# lzmfmx.h includes "Fail.h"/"Debug.h"/"XUtils.h" with quotes, which find the real
# ones next to it first, so build from a temp copy of the real lzmfmx and its
# small dependencies sitting beside the shims.
set -e
cd "$(dirname "$0")"
SH=../../srcs/shared
T=$(mktemp -d)
cp shim/*.h "$T"/
cp $SH/include/lzmfmx.h $SH/include/RingBuffer.h $SH/include/itype.h "$T"/
cp $SH/src/lzmfmx.cpp "$T"/
cp lzrestart_test.cpp "$T"/
g++ -std=c++20 -O1 -Wall -DBUILD_HOST -I"$T" "$T"/lzrestart_test.cpp "$T"/lzmfmx.cpp -o "$T"/t
"$T"/t
