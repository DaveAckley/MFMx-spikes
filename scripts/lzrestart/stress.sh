#!/bin/bash
# lzmfmx stress test (see lzstress_test.cpp).  usage: stress.sh [SEED [STREAMS [MB]]]
set -e
cd "$(dirname "$0")"
SH=../../srcs/shared
T=$(mktemp -d)
cp shim/*.h "$T"/
cp $SH/include/lzmfmx.h $SH/include/RingBuffer.h $SH/include/itype.h "$T"/
cp $SH/src/lzmfmx.cpp "$T"/
cp lzstress_test.cpp "$T"/
g++ -std=c++20 -O2 -Wall -DBUILD_HOST -I"$T" "$T"/lzstress_test.cpp "$T"/lzmfmx.cpp -o "$T"/s
"$T"/s "${1:-1}" "${2:-4}" "${3:-16}"
