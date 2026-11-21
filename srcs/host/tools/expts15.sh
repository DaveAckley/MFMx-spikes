#!/bin/bash
# expts15.sh -- T6 -> ERISC over NoC1 (t6pp3 + ethfwd21 --t6-noc) for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# QUESTION: does a Tensix NoC write INTO an ETH tile work over NIU1 (NoC1), with node ids passed as raw NoC0 coordinates the way MFMx passes
# them to NIU1 for T6 -> T6?  NRI3::initiateWriteToEthL1 is forced to NoC0 only because this was never tried; the architectural wish is to
# let preferNoC choose (NoC1 for the upper Tensix rows, NoC0 for the lower rows, the torus wraps in Y), so spatial layout shows in the traffic.
# E1 -> T6 over NoC1 is PROVEN (out14 steps 30-33); this is the other leg.  The ping-pong is the same stage-3a rig as expts12/13/14: a bare-metal
# image on a Tensix tile of EACH card sends cars to its local E1 (T6 -> ETH write, the leg under test, --t6-noc), the E1 ships them over the link,
# the peer E1 NoC-writes them into the peer tile (--e1-noc, the proven leg) where the image reflects them.  Every car carries a checkable payload.
#
# Steps: build; read-only NIU dump; a dry run; CONTROL (T6 writes over NoC0, as always); a PROBE (T6 writes over NoC1, one channel, small) -- a
# probe that fails is reported and the batch continues with the control-only summary; if it worked: sizes 64/576/4160, two channels, E1 replies
# over NoC0 and over NoC1, three tile pairs spread over the Tensix rows (upper rows prefer NoC1 by hop count, lower rows prefer NoC0), and a 60 s soak.
# WARNING: a T6 -> ETH write over NoC1 that does not reach the ETH tile may land on another tile (the NoC1 mirror of the ETH tile (x,1) is a Tensix
# tile in row 10) or nowhere; tiles must be FREE.  The expected failure is a timeout or bad payload in the result, as in expts14.
# T6 tile pairs below are NoC0 coordinates (Tensix columns x 1..7, 10..16; rows y 2..11); a harvested tile exits with code 4 (WARNING, continues).
#
# Run from anywhere:   bash expts15.sh   (bash, not sh).   A few minutes.   Output in ./out15/ (SUMMARY.txt first); afterwards
#   scp -r ./out15 <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude.
# Testing the script without hardware:   EXTRA=--mock OUT15=/some/dir bash expts15.sh      (all numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT15:-out15}
X=${EXTRA:-}
LINK="--link 0:1:6"
IMG="--image e1fwd/e1fwd16.bin --t6-image t6pp/t6pp3.bin"
T=./ethfwd21
mkdir -p "$OUT" || { echo "cannot create $OUT"; exit 1; }
rm -f "$OUT"/*.txt

banner() {
  echo
  echo "################################################################################"
  for l in "$@"; do echo "####  $l"; done
  echo "################################################################################"
}
fail() {   # $1 = step name
  banner "EXPERIMENT FAILED: $1" \
         "Output of the failing step: $OUT/$1.txt   (last lines below)" \
         "STOPPING: later experiments were NOT run.  Do not rerun blindly." \
         "ethfwd21 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out15/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
step() {
  local name=$1; shift
  banner "STEP $name" "$*"
  echo "# $*" > "$OUT/$name.txt"
  if ! { "$@" 2>&1 | tee -a "$OUT/$name.txt"; }; then fail "$name"; fi
  sleep 1
}

# soft_step NAME CMD... : like step, but exit code 4 (requested T6 tile harvested/unavailable) is only a loud WARNING
soft_step() {
  local name=$1; shift
  banner "STEP $name (tile-tolerant)" "$*"
  echo "# $*" > "$OUT/$name.txt"
  "$@" 2>&1 | tee -a "$OUT/$name.txt"
  local rc=${PIPESTATUS[0]}
  if [ "$rc" = 4 ]; then
    banner "WARNING: step $name SKIPPED: a requested T6 tile is harvested/unavailable (tool exit code 4)" "Nothing was started; continuing with the next step."
    echo "SKIPPED (tile unavailable)" >> "$OUT/$name.txt"
  elif [ "$rc" != 0 ]; then fail "$name"; fi
  sleep 1
}
# try_step NAME CMD... : a probe.  Records success/failure in PROBE_OK[NAME] and never stops the batch.
declare -A PROBE_OK
try_step() {
  local name=$1; shift
  banner "PROBE $name" "$*"
  echo "# $*" > "$OUT/$name.txt"
  "$@" 2>&1 | tee -a "$OUT/$name.txt"
  local rc=${PIPESTATUS[0]}
  if [ "$rc" = 0 ]; then PROBE_OK[$name]=1; echo "PROBE $name: WORKED" | tee -a "$OUT/$name.txt"
  else PROBE_OK[$name]=0; banner "PROBE $name FAILED (rc=$rc)  -- continuing with the other probes" "$OUT/$name.txt"; echo "PROBE FAILED rc=$rc" >> "$OUT/$name.txt"; fi
  sleep 1
}
# ------------------------------------------------------------------------------------------------------------------------------
{ echo "date: $(date -Is)"; echo "host: $(uname -a)"; echo "git: $(git rev-parse HEAD 2>&1) $(git status --short 2>&1 | grep -v '^??' | head -5)";
  echo "gcc: $(g++ --version | head -1)"; echo "extra args: '$X'"; } > "$OUT/00_env.txt"

PPC="--nch 2 --ks 4 --sizes 64,576,4160 --csr --t6-hart nc --nri-buf 3"   # common ping-pong options (as expts14 steps 16-17)

step 01_build_tool       g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd21.cpp -o ethfwd21 \
&& step 02_build_e1fwd16 sh e1fwd/build.sh e1fwd16 \
&& step 03_build_t6pp3   sh t6pp/build.sh t6pp3 \
&& step 04_dump_niu      $T $LINK --dump-niu $X \
&& step 05_dryrun        $T $LINK $IMG --pp --nch 1 --t6-noc 1 $X \
&& step 10_control_noc0  $T $LINK $IMG --go --pp $PPC $X \
|| exit 1

try_step 20_probe_t6_noc1 $T $LINK $IMG --go --pp --nch 1 --ks 1 --sizes 64 --csr --t6-hart nc --nri-buf 3 --t6-noc 1 $X
if [ "${PROBE_OK[20_probe_t6_noc1]}" = 1 ]; then
  banner "T6 -> ETH over NoC1 WORKED in the probe: running the matrix"
  step 21_t6noc1_e1noc0       $T $LINK $IMG --go --pp $PPC --t6-noc 1 $X \
  && step 22_t6noc1_e1noc1    $T $LINK $IMG --go --pp $PPC --t6-noc 1 --e1-noc 1 $X \
  && soft_step 23_upper_row   $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 3:2 --t6b 3:2 $X \
  && soft_step 24_mid_row     $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 5:6 --t6b 5:6 $X \
  && soft_step 25_lower_row   $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 12:10 --t6b 12:10 $X \
  && soft_step 26_lower_ctrl  $T $LINK $IMG --go --pp $PPC --t6a 12:10 --t6b 12:10 $X \
  && step 27_soak_noc1_60s    $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 576 --dur-ms 60000 --csr --t6-hart nc --nri-buf 3 --t6-noc 1 $X \
  || exit 1
else
  banner "T6 -> ETH over NoC1 FAILED or timed out in the probe (NIU1 does not deliver raw-NoC0-coordinate writes into the ETH tile as configured)" \
         "Skipping the matrix; see $OUT/20_probe_t6_noc1.txt and the NIU dump in $OUT/04_dump_niu.txt"
fi

{ echo "== RESULT / PROBE lines =="; grep -H "^RESULT\|PROBE" "$OUT"/[0-9]*.txt;
  echo; echo "== NIU dump =="; grep -h "NIU[01] buf" "$OUT/04_dump_niu.txt";
  echo; echo "== link health deltas =="; grep -H "delta over the run" "$OUT"/[1-2][0-9]_*.txt;
  echo; echo "== pp (control step 10 and 26 are NoC0; 20-25, 27 are T6 over NoC1) =="; grep -H "round trips/s" "$OUT"/1[0-9]_*.txt "$OUT"/2[0-9]_*.txt 2>/dev/null; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS DONE" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
