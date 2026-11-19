#!/bin/bash
# Fails if HubGeometry.py (Python) disagrees with DemoGlobal.h (C++).
# Run from the repo root: scripts/hubgeom/hubgeom_check.sh
set -e
T=$(mktemp -d)
g++ -std=c++17 -DBUILD_HOST -Isrcs/shared/include -Isrcs/host/include \
  scripts/hubgeom/hubgeom_dump.cpp srcs/shared/src/S8C.cpp -o "$T/dump"
"$T/dump" > "$T/cpp.txt"
python3 -I srcs/host/python/EWD/HubGeometry.py > "$T/py.txt"
if diff "$T/cpp.txt" "$T/py.txt"; then
  echo "PASS hubgeom: Python and C++ geometry agree"
  rm -rf "$T"
else
  echo "FAIL hubgeom: HubGeometry.py and DemoGlobal.h disagree"
  rm -rf "$T"
  exit 1
fi
