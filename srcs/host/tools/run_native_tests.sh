#!/bin/bash
# run_native_tests.sh -- build and run every native (no hardware, no pybind)
# test of the eth-seam work, from anywhere:
#
#     srcs/host/tools/run_native_tests.sh            # all of them
#     srcs/host/tools/run_native_tests.sh ethlinks   # those whose name matches
#     ASAN=1 srcs/host/tools/run_native_tests.sh     # C++ tests under ASan/UBSan
#
# Needs g++ and gcc (no riscv toolchain).  Builds into a temp directory and
# prints one PASS/FAIL line per test; exits nonzero if any test fails or
# does not build.
#
# The tests that include MFMx headers (MDist.h, DemoGlobal.h) build with
# -I host/include for its FATAL.h and link host/src/FATAL.cpp (unoptimized
# builds reference DieHereNow), and need -Wno-unused-parameter /
# -Wno-unused-function because those headers carry unused statics.

HERE=$(cd "$(dirname "$0")" && pwd)
SRCS=$(cd "$HERE/../.." && pwd)
INC="-I$SRCS/host/include -I$SRCS/shared/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter"
if [ "${ASAN:-0}" = 1 ]; then
  CXXFLAGS="-std=c++17 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined"
fi
CFLAGS="-O2 -Wall -pthread"
WANT="$1"

OUT=$(mktemp -d /tmp/native-tests.XXXXXX)
trap 'rm -rf "$OUT"' EXIT
pass=0
fail=0

# run NAME BUILD-COMMAND...   (the build command writes $OUT/NAME)
run() {
  local name=$1
  shift
  if [ -n "$WANT" ] && [[ "$name" != *$WANT* ]]; then
    return
  fi
  if ! "$@" > "$OUT/$name.build" 2>&1; then
    echo "FAIL  $name (does not build)"
    sed 's/^/        /' "$OUT/$name.build" | head -15
    fail=$((fail + 1))
    return
  fi
  if "$OUT/$name" > "$OUT/$name.log" 2>&1; then
    echo "PASS  $name   $(tail -1 "$OUT/$name.log" | cut -c1-60)"
    pass=$((pass + 1))
  else
    echo "FAIL  $name"
    # the failing checks first, then the end of the log
    grep -a "FAIL" "$OUT/$name.log" | head -10 | sed 's/^/        /'
    tail -4 "$OUT/$name.log" | sed 's/^/        /'
    fail=$((fail + 1))
  fi
}

cd "$HERE"
run ethseam_test g++ $CXXFLAGS $INC ethseam_test.cpp ../src/FATAL.cpp \
  -o "$OUT/ethseam_test"
run hubparity_test g++ $CXXFLAGS $INC hubparity_test.cpp ../src/FATAL.cpp \
  -o "$OUT/hubparity_test"
run ethlinks_test g++ $CXXFLAGS $INC ethlinks_test.cpp \
  ../src/EthLinks.cpp ../src/EthSeamHost.cpp ../src/FATAL.cpp \
  -o "$OUT/ethlinks_test"
for n in 13 14 15 16; do
  run e1fwd_hosttest$n gcc $CFLAGS e1fwd/hosttest$n.c -o "$OUT/e1fwd_hosttest$n"
done
run t6pp_hosttest gcc $CFLAGS t6pp/hosttest.c -o "$OUT/t6pp_hosttest"
run t6pp_hosttest2 gcc $CFLAGS t6pp/hosttest2.c -o "$OUT/t6pp_hosttest2"
run t6pp_hosttest3 gcc $CFLAGS t6pp/hosttest3.c -o "$OUT/t6pp_hosttest3"
# arcpeek (read-only ARC/telemetry reader): build it, then a runner script checks its --mock table walk
run arcpeek_mock sh -c 'g++ '"$CXXFLAGS"' arcpeek.cpp -o "$1/arcpeek_bin" && printf "#!/bin/sh\n\"%s/arcpeek_bin\" --mock | grep -q \"low16 = 800\" && \"%s/arcpeek_bin\" --mock | grep -q \"ARC alive\" && echo mock-table-walk-ok\n" "$1" "$1" > "$1/arcpeek_mock" && chmod +x "$1/arcpeek_mock"' x "$OUT"
# arcmsg (TEST + GET_AICLK on the ARC message queue): mock queue round trip, and no-hardware without --dev
run arcmsg_mock sh -c 'g++ '"$CXXFLAGS"' arcmsg.cpp -o "$1/arcmsg_bin" && printf "#!/bin/sh\n\"%s/arcmsg_bin\" --mock | grep -q \"TRANSPORT OK\" && \"%s/arcmsg_bin\" --mock | grep -q \"AICLK 800 MHz\" && ! \"%s/arcmsg_bin\" 2>/dev/null && echo mock-queue-ok\n" "$1" "$1" "$1" > "$1/arcmsg_mock" && chmod +x "$1/arcmsg_mock"' x "$OUT"
# t6arc (T6 reads the ARC telemetry over the NoC): image logic against a fake NoC, then the host runner's mock run
run t6arc_hosttest gcc $CFLAGS t6pp/hosttest_arc.c -o "$OUT/t6arc_hosttest"
# (the mock never executes the image, so any small file stands in for the .bin: no riscv toolchain needed)
run t6arcrun_mock sh -c 'g++ '"$CXXFLAGS"' t6arcrun.cpp -o "$1/t6arcrun_bin" && printf "#!/bin/sh\ncd \"%s\" || exit 1\n\"%s/t6arcrun_bin\" --mock --go --image \"%s\" | grep -q \"T6 READ OK\" && echo mock-run-ok\n" "$(pwd)" "$1" "$(pwd)/t6pp/t6arc.c" > "$1/t6arcrun_mock" && chmod +x "$1/t6arcrun_mock"' x "$OUT"

echo
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
